#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "processingdialog.h"
#include "processing/stardetector.h"
#include "model/imagerecord.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QGraphicsPixmapItem>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/video/video.hpp>
#include <QDesktopWidget>
#ifdef WIN32
#include <QtWinExtras/QWinTaskbarButton>
#include <QtWinExtras/QWinTaskbarProgress>
#endif

using namespace cv;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // centre window
    QRect desktopRect = QApplication::desktop()->availableGeometry(this);
    QPoint center = desktopRect.center();
    this->move(center.x() - this->width()*0.5, center.y() - this->height()*0.5);

    // cv::Mat can't be passed through a signal without this declaration
    qRegisterMetaType<cv::Mat>("cv::Mat");

    stacker = new ImageStacker();
    stacker->setBitsPerChannel(ImageStacker::BITS_32);
    workerThread = new QThread();

    stacker->moveToThread(workerThread);
    workerThread->start();

    imageFileFilter << "All files (*)" << "Image files (*.jpg *.jpeg *.png *.tif)";
    imageFileFilter << "Raw image files (*.NEF *.CR2 *.DNG *.RAW)";

    QTableView *table = ui->imageListView;
    table->setModel(&tableModel);
    table->setColumnWidth(0,260);
    table->setColumnWidth(1,80);
    table->setColumnWidth(2,80);
    table->setColumnWidth(3,60);
    table->setColumnWidth(4,140);

    table->setContextMenuPolicy(Qt::CustomContextMenu);

    //connect(ui->buttonSelectRefImage, SIGNAL (released()), this, SLOT (handleButtonRefImage()));
    connect(ui->buttonSelectLightFrames, SIGNAL (released()), this, SLOT (handleButtonLightFrames()));
    connect(ui->buttonSelectDarkFrames, SIGNAL (released()), this, SLOT (handleButtonDarkFrames()));
    connect(ui->buttonSelectDarkFlatFrames, SIGNAL (released()), this, SLOT (handleButtonDarkFlatFrames()));
    connect(ui->buttonSelectFlatFrames, SIGNAL (released()), this, SLOT (handleButtonFlatFrames()));
    connect(stacker, SIGNAL(updateProgress(QString,int)), this, SLOT(updateProgress(QString,int)));
    connect(table,SIGNAL(customContextMenuRequested(QPoint)),this,SLOT(showTableContextMenu(QPoint)));

    connect(ui->buttonStack, SIGNAL (released()), this, SLOT (handleButtonStack()));
    connect(this, SIGNAL (stackImages()), stacker, SLOT(process()));
    connect(stacker, SIGNAL(finished(cv::Mat)), this, SLOT(finishedStacking(cv::Mat)));
    connect(stacker, SIGNAL(finishedDialog(QString)), this, SLOT(clearProgress(QString)));
}

void MainWindow::finishedStacking(Mat image) {
    QString path = stacker->getSaveFilePath();
    imwrite(path.toUtf8().constData(), image);
    setMemImage(Mat2QImage(image));

    qDebug() << "Done stacking";
}

void MainWindow::updateProgress(QString message, int percentComplete)
{
    Q_UNUSED(message);
    Q_UNUSED(percentComplete);
#ifdef WIN32
    QWinTaskbarButton *button = new QWinTaskbarButton(this);
    button->setWindow(this->windowHandle());

    QWinTaskbarProgress *progress = button->progress();
    progress->setVisible(true);
    progress->setValue(percentComplete);
#endif // WIN32
}

void MainWindow::clearProgress(QString message)
{
    Q_UNUSED(message);
#ifdef WIN32
    QWinTaskbarButton *button = new QWinTaskbarButton(this);
    button->setWindow(this->windowHandle());

    QWinTaskbarProgress *progress = button->progress();
    progress->setVisible(false);
#endif // WIN32
}

void MainWindow::showTableContextMenu(QPoint pos)
{
    QMenu *menu = new QMenu(this);
    QTableView *table = ui->imageListView;

    QAction *setAsReferenceAction = new QAction("Set As Reference", this);
    connect(setAsReferenceAction, SIGNAL(triggered(bool)), this,SLOT(setFrameAsReference()));
    menu->addAction(setAsReferenceAction);
    menu->popup(table->viewport()->mapToGlobal(pos));
}

void MainWindow::setFrameAsReference()
{
    QTableView *table = ui->imageListView;
    QItemSelectionModel *select = table->selectionModel();
    ImageTableModel *model = (ImageTableModel*)table->model();
    QModelIndexList rows = select->selectedRows();

    if (rows.count() > 1) {
        QMessageBox msg;
        msg.setText("Cannot set more than one frame as the reference frame.");
        msg.exec();
        return;
    }

    clearReferenceFrame();
    int i = rows.at(0).row();
    qDebug() << "Set reference frame, row" << i;
    ImageRecord *record = model->at(i);
    record->setReference(true);
}

void MainWindow::handleButtonStack() {

    QString saveFilePath = QFileDialog::getSaveFileName(
                this, "Select Output Image", selectedDir.absolutePath(), "TIFF Image (*.tif)");

    if (saveFilePath.isEmpty()) {
        qDebug() << "No output file selected. Cancelling.";
        return;
    }

    stacker->setSaveFilePath(saveFilePath);

    QTableView *table = ui->imageListView;
    ImageTableModel *model = (ImageTableModel*)table->model();

    bool referenceSet = false;
    for (int i = 0; i < model->rowCount(); i++) {
        ImageRecord *record = model->at(i);
        if (record->isReference()) referenceSet = true;
    }

    if (!referenceSet) {
        for (int i = 0; i < model->rowCount(); i++) {
            ImageRecord *record = model->at(i);

            if (record->getType() == ImageRecord::LIGHT) {
                record->setReference(true);
                break;
            }
        }
    }

    QStringList lights;
    QStringList darks;
    QStringList darkFlats;
    QStringList flats;

    for (int i = 0; i < model->rowCount(); i++) {
        ImageRecord *record = model->at(i);
        QString filename = record->getFilename();

        switch (record->getType()) {
        case ImageRecord::LIGHT:
            if (record->isReference()) {
                stacker->setRefImageFileName(filename);
                break;
            }

            lights.append(filename);
            break;
        case ImageRecord::DARK:
            darks.append(filename);
            stacker->setUseDarks(true);
            break;
        case ImageRecord::DARK_FLAT:
            darkFlats.append(filename);
            stacker->setUseDarkFlats(true);
            break;
        case ImageRecord::FLAT:
            flats.append(filename);
            stacker->setUseFlats(true);
            break;
        default:
            break;
        }
    }

    stacker->setTargetImageFileNames(lights);
    stacker->setDarkFrameFileNames(darks);
    stacker->setDarkFlatFrameFileNames(darkFlats);
    stacker->setFlatFrameFileNames(flats);

    // asynchronously trigger the processing
    emit stackImages();

    ProcessingDialog *dialog = new ProcessingDialog(this);
    connect(stacker, SIGNAL(updateProgress(QString,int)), dialog, SLOT(updateProgress(QString,int)));
    connect(stacker, SIGNAL(finishedDialog(QString)), dialog, SLOT(complete(QString)));

    if (!dialog->exec()) {
        qDebug() << "Cancelling...";
        stacker->cancel = true;
    }
}

//void MainWindow::handleButtonRefImage() {
//    QFileDialog dialog(this);
//    dialog.setDirectory(QDir::homePath());
//    dialog.setFileMode(QFileDialog::ExistingFile);
//    dialog.setNameFilters(imageFileFilter);

//    if (!dialog.exec()) return;

//    ImageRecord *record = stacker->getImageRecord(refImageFileName);
//    record->setType(ImageRecord::LIGHT);
//    record->setReference(true);

//    tableModel.append(record);

//    QFileInfo info(refImageFileName);
//    selectedDir = QDir(info.absoluteFilePath());
//    setFileImage(refImageFileName);
//    qDebug() << refImageFileName;
//}

void MainWindow::handleButtonLightFrames() {
    QFileDialog dialog(this);
    dialog.setDirectory(selectedDir);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilters(imageFileFilter);

    if (!dialog.exec()) return;

    QStringList targetImageFileNames = dialog.selectedFiles();

    for (int i = 0; i < targetImageFileNames.length(); i++) {
        ImageRecord *record = stacker->getImageRecord(targetImageFileNames.at(i));
        record->setType(ImageRecord::LIGHT);
        tableModel.append(record);
    }

    ui->buttonStack->setEnabled(true);
}

void MainWindow::handleButtonDarkFrames() {
    QFileDialog dialog(this);
    dialog.setDirectory(selectedDir);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilters(imageFileFilter);

    if (!dialog.exec()) return;

    QStringList darkFrameFileNames = dialog.selectedFiles();

    for (int i = 0; i < darkFrameFileNames.length(); i++) {
        ImageRecord *record = stacker->getImageRecord(darkFrameFileNames.at(i));
        record->setType(ImageRecord::DARK);
        tableModel.append(record);
    }
}

void MainWindow::handleButtonDarkFlatFrames() {
    QFileDialog dialog(this);
    dialog.setDirectory(selectedDir);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilters(imageFileFilter);

    if (!dialog.exec()) return;

    QStringList darkFlatFrameFileNames = dialog.selectedFiles();

    for (int i = 0; i < darkFlatFrameFileNames.length(); i++) {
        ImageRecord *record = stacker->getImageRecord(darkFlatFrameFileNames.at(i));
        record->setType(ImageRecord::DARK_FLAT);
        tableModel.append(record);
    }
}

void MainWindow::handleButtonFlatFrames() {
    QFileDialog dialog(this);
    dialog.setDirectory(selectedDir);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilters(imageFileFilter);

    if (!dialog.exec()) return;

    QStringList flatFrameFileNames = dialog.selectedFiles();

    for (int i = 0; i < flatFrameFileNames.length(); i++) {
        ImageRecord *record = stacker->getImageRecord(flatFrameFileNames.at(i));
        record->setType(ImageRecord::FLAT);
        tableModel.append(record);
    }
}

QImage MainWindow::Mat2QImage(const cv::Mat &src) {
        QImage dest(src.cols, src.rows, QImage::Format_RGB32);
        int r, g, b;

        if (stacker->getBitsPerChannel() == ImageStacker::BITS_16) {
            for(int x = 0; x < src.cols; x++) {
                for(int y = 0; y < src.rows; y++) {

                    Vec<unsigned short,3> pixel = src.at<Vec<unsigned short,3>>(y,x);
                    b = pixel.val[0]/256;
                    g = pixel.val[1]/256;
                    r = pixel.val[2]/256;
                    dest.setPixel(x, y, qRgb(r,g,b));
                }
            }
        }
        else if (stacker->getBitsPerChannel() == ImageStacker::BITS_32) {
            for(int x = 0; x < src.cols; x++) {
                for(int y = 0; y < src.rows; y++) {

                    Vec3f pixel = src.at<Vec3f>(y,x);
                    b = pixel.val[0]*255;
                    g = pixel.val[1]*255;
                    r = pixel.val[2]*255;
                    dest.setPixel(x, y, qRgb(r,g,b));
                }
            }
        }
        return dest;
}


void MainWindow::setFileImage(QString filename) {

    QGraphicsScene* scene = new QGraphicsScene(this);
    cv::Mat image = stacker->readImage(filename);
    QGraphicsPixmapItem *p = scene->addPixmap(QPixmap::fromImage(Mat2QImage(image)));
    ui->imageHolder->setScene(scene);
    ui->imageHolder->fitInView(p, Qt::KeepAspectRatio);
}

void MainWindow::setMemImage(QImage image) {
    QGraphicsScene* scene = new QGraphicsScene(this);
    QGraphicsPixmapItem *p = scene->addPixmap(QPixmap::fromImage(image));
    ui->imageHolder->setScene(scene);
    ui->imageHolder->fitInView(p, Qt::KeepAspectRatio);
}

void MainWindow::clearReferenceFrame()
{
    QTableView *table = ui->imageListView;
    ImageTableModel *model = (ImageTableModel*)table->model();

    for (int i = 0; i < model->rowCount(); i++) {
        ImageRecord *record = model->at(i);
        record->setReference(false);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
    workerThread->quit();
    workerThread->wait();
    delete workerThread;
    delete stacker;
}