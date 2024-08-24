#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "m3u8d.h"
#include <atomic>
#include <QFileDialog>
#include <QIntValidator>
#include <QMessageBox>
#include <QFileDialog>
#include <QTimer>
#include <QDebug>
#include <QCloseEvent>
#include "curldialog.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    ui->lineEdit_SaveDir->setPlaceholderText(QString::fromStdString(GetWd()));
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, [this](){
        //更新ui1
        {
            GetStatus_Resp resp = GetStatus();
            ui->progressBar->setValue(resp.Percent);
            ui->label_progressBar->setText(QString::fromStdString(resp.Title));
            if(!resp.StatusBar.empty())
                ui->statusBar->showMessage(QString::fromStdString(resp.StatusBar), 5*1000);
            updateDownloadUi(resp.IsDownloading);
        }

        //更新ui2
        {
            auto resp = MergeGetProgressPercent();
            ui->progressBar_merge->setValue(resp.Percent);
            if(!resp.SpeedText.empty())
                ui->statusBar->showMessage(QString::fromStdString(resp.SpeedText), 5*1000);
        }
    });
    m_timer->start(50);
    this->updateDownloadUi(false);
    this->updateMergeUi(false);

    m_saveConfigTimer = new QTimer(this);
    m_saveConfigTimer->start(3000);
    connect(m_saveConfigTimer, &QTimer::timeout, [this](){
        saveUiConfig();
    });

    loadUiConfig();
    setWindowTitle("m3u8d-" + QString::fromStdString(GetVersion()));
}

MainWindow::~MainWindow()
{
    m_timer->stop();
    m_saveConfigTimer->stop();
    CloseOldEnv();
    saveUiConfig();
    delete ui;
}

void MainWindow::on_pushButton_RunDownload_clicked()
{
    if (ui->lineEdit_M3u8Url->isEnabled()==false) {
        return;
    }
    StartDownload_Req req;
    req.M3u8Url = ui->lineEdit_M3u8Url->text().toStdString();
    req.Insecure = ui->checkBox_Insecure->isChecked();
    req.SaveDir = ui->lineEdit_SaveDir->text().toStdString();
    req.FileName = ui->lineEdit_FileName->text().toStdString();
    req.SkipTsExpr = ui->lineEdit_SkipTsExpr->text().toStdString();
    req.SetProxy = ui->lineEdit_SetProxy->text().toStdString();
    req.HeaderMap = m_HeaderMap;
    req.SkipRemoveTs = ui->checkBox_SkipRemoveTs->isChecked();
    req.ThreadCount = ui->lineEdit_ThreadCount->text().toInt();
    req.SkipMergeTs = ui->checkBox_SkipMergeTs->isChecked();
    req.Skip_EXT_X_DISCONTINUITY = ui->checkBox_Skip_EXT_X_DISCONTINUITY->isChecked();
    req.DebugLog = ui->checkBox_DebugLog->isChecked();

    std::string errMsg = StartDownload(req);
    if(!errMsg.empty()) {
        Toast::Instance()->SetError(QString::fromStdString(errMsg));
        return;
    }
    m_syncUi.AddRunFnOn_OtherThread([=](){
        GetStatus_Resp resp = WaitDownloadFinish();
        m_syncUi.AddRunFnOn_UiThread([=](){
            if (resp.IsCancel) {
                return;
            }

            if (!resp.ErrMsg.empty()) {
                QMessageBox::warning(this, "下载错误", QString::fromStdString(resp.ErrMsg));
                return;
            }
            if (resp.IsSkipped) {
                Toast::Instance()->SetSuccess("已经下载过了: " + QString::fromStdString(resp.SaveFileTo));
                return;
            }
            if (resp.SaveFileTo.empty()) {
                Toast::Instance()->SetSuccess("下载成功");
                return;
            }
            Toast::Instance()->SetSuccess("下载成功, 保存路径" + QString::fromStdString(resp.SaveFileTo));
        });
    });
}

void MainWindow::on_pushButton_SaveDir_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this);
    ui->lineEdit_SaveDir->setText(dir);
}

void MainWindow::on_pushButton_StopDownload_clicked()
{
    CloseOldEnv();
}

void MainWindow::on_pushButton_curlMode_clicked()
{
    StartDownload_Req req;
    req.M3u8Url = ui->lineEdit_M3u8Url->text().toStdString();
    req.Insecure = ui->checkBox_Insecure->isChecked();
    req.HeaderMap = m_HeaderMap;
    CurlDialog dlg(this);
    dlg.SetText(QString::fromStdString(RunDownload_Req_ToCurlStr(req)));
    dlg.show();
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    ParseCurl_Resp resp= dlg.Resp;
    ui->lineEdit_M3u8Url->setText(QString::fromStdString(resp.DownloadReq.M3u8Url));
    ui->checkBox_Insecure->setChecked(resp.DownloadReq.Insecure);
    this->m_HeaderMap = resp.DownloadReq.HeaderMap;
}

void MainWindow::on_pushButton_returnDownload_clicked()
{
    ui->stackedWidget->setCurrentIndex(0);
}

void MainWindow::on_pushButton_gotoMergeTs_clicked()
{
    ui->stackedWidget->setCurrentIndex(1);
}

void MainWindow::on_pushButton_startMerge_clicked()
{
    QString fileName = ui->lineEdit_mergeFileName->text();
    if(fileName.isEmpty())
        fileName = ui->lineEdit_mergeFileName->placeholderText();
    QString dir = ui->lineEdit_mergeDir->text();

    this->updateMergeUi(true);

    m_syncUi.AddRunFnOn_OtherThread([=](){
        auto resp = MergeTsDir(dir.toStdString(), fileName.toStdString());

        m_syncUi.AddRunFnOn_UiThread([=](){
            this->updateMergeUi(false);
            if(resp.ErrMsg.empty())
                Toast::Instance()->SetSuccess("合并成功!");
            else if(!resp.IsCancel)
                QMessageBox::warning(this, "合并错误", QString::fromStdString(resp.ErrMsg));
        });
    });
}

void MainWindow::on_pushButton_stopMerge_clicked()
{
    MergeStop();
}

void MainWindow::on_toolButton_selectMergeDir_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this);
    if(dir.isEmpty())
        return;
    ui->lineEdit_mergeDir->setText(dir);
}

void MainWindow::updateDownloadUi(bool runing)
{
    ui->lineEdit_M3u8Url->setEnabled(!runing);
    ui->lineEdit_SaveDir->setEnabled(!runing);
    ui->pushButton_SaveDir->setEnabled(!runing);
    ui->lineEdit_FileName->setEnabled(!runing);
    ui->lineEdit_SkipTsExpr->setEnabled(!runing);
    ui->pushButton_RunDownload->setEnabled(!runing);
    ui->checkBox_Insecure->setEnabled(!runing);
    if(runing == false)
        ui->pushButton_RunDownload->setText("开始下载");
    ui->lineEdit_SetProxy->setEnabled(!runing);
    ui->pushButton_StopDownload->setEnabled(runing);
    ui->pushButton_curlMode->setEnabled(!runing);
    ui->checkBox_SkipRemoveTs->setEnabled(!runing);
    ui->lineEdit_ThreadCount->setEnabled(!runing);
    ui->checkBox_SkipMergeTs->setEnabled(!runing);
    ui->checkBox_Skip_EXT_X_DISCONTINUITY->setEnabled(!runing);
    ui->checkBox_DebugLog->setEnabled(!runing);

    if(runing == false)
        ui->progressBar->setValue(0);

    ui->pushButton_gotoMergeTs->setEnabled(!runing);
}

void MainWindow::updateMergeUi(bool runing)
{
    ui->lineEdit_mergeDir->setEnabled(!runing);
    ui->toolButton_selectMergeDir->setEnabled(!runing);
    ui->pushButton_stopMerge->setEnabled(runing);
    ui->pushButton_startMerge->setEnabled(!runing);
    ui->lineEdit_mergeFileName->setEnabled(!runing);
    ui->pushButton_returnDownload->setEnabled(!runing);
}

static const QString configPath = "m3u8d_config.json";

void MainWindow::saveUiConfig()
{

}

void MainWindow::loadUiConfig()
{

}

void MainWindow::closeEvent(QCloseEvent *event)
{
    do {
        GetStatus_Resp status = GetStatus();
        if(!status.IsDownloading) {
            break;
        }
        // 创建 QMessageBox 对象
        QMessageBox msgBox;
        // 设置标题和消息
        msgBox.setText("正在下载文件，是否关闭窗口？");
        msgBox.setWindowTitle("确认关闭窗口");

        // 添加按钮并设置默认按钮
        msgBox.addButton(QMessageBox::Yes);
        msgBox.addButton(QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::No);

        // 显示弹窗并根据用户选择执行相应操作
        int result = msgBox.exec();
        if (result != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    } while(false);

    event->accept();
}

void MainWindow::on_lineEdit_M3u8Url_editingFinished()
{
    QString originUrl = ui->lineEdit_M3u8Url->text();
    QString urlStr = QString::fromStdString(FindUrlInStr(originUrl.toStdString()));
    if(urlStr.isEmpty()) {
        if(originUrl.isEmpty() == false)
        {
            Toast::Instance()->SetWaring("m3u8 url不合法");
        }
        return;
    }
    if(urlStr != originUrl) {
        ui->lineEdit_M3u8Url->setText(urlStr);
        Toast::Instance()->SetTips("自动识别修改了url");
        return;
    }
    QString fileName = QString::fromStdString(GetFileNameFromUrl(urlStr.toStdString()));
    if (fileName.isEmpty()) {
        return;
    }
    ui->lineEdit_FileName->setPlaceholderText(fileName);
}
