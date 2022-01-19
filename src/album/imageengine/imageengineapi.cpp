/*
 * Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
 *
 * Author:     ZhangYong <zhangyong@uniontech.com>
 *
 * Maintainer: ZhangYong <ZhangYong@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "imageengineapi.h"
#include "DBandImgOperate.h"
#include "controller/signalmanager.h"
#include "application.h"
#include "imageengineapi.h"
#include <QMetaType>
#include <QDirIterator>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include "utils/unionimage.h"
#include "utils/baseutils.h"
#include "albumgloabl.h"
#include "imagedataservice.h"

ImageEngineApi *ImageEngineApi::s_ImageEngine = nullptr;

ImageEngineApi *ImageEngineApi::instance(QObject *parent)
{
    Q_UNUSED(parent);
    if (!s_ImageEngine) {
        s_ImageEngine = new ImageEngineApi();
    }
    return s_ImageEngine;
}

ImageEngineApi::~ImageEngineApi()
{
#ifdef NOGLOBAL
    m_qtpool.clear();
    m_qtpool.waitForDone();
    cacheThreadPool.clear();
    cacheThreadPool.waitForDone();
#else
    QThreadPool::globalInstance()->clear();     //清除队列
    QThreadPool::globalInstance()->waitForDone();
#endif
}

ImageEngineApi::ImageEngineApi(QObject *parent)
{
    Q_UNUSED(parent);
    //文件加载线程池上限

    qRegisterMetaType<QStringList>("QStringList &");
    qRegisterMetaType<DBImgInfo>("ImageDataSt &");
    qRegisterMetaType<DBImgInfo>("ImageDataSt");
    qRegisterMetaType<DBImgInfoList>("DBImgInfoList");
    qRegisterMetaType<QMap<QString, DBImgInfo>>("QMap<QString,DBImgInfo>");
    qRegisterMetaType<QVector<DBImgInfo>>("QVector<ImageDataSt>");
#ifdef NOGLOBAL
    m_qtpool.setMaxThreadCount(4);
    cacheThreadPool.setMaxThreadCount(4);
#else
    QThreadPool::globalInstance()->setMaxThreadCount(12);
    QThreadPool::globalInstance()->setExpiryTimeout(10);
#endif

    bcloseFg = false;
}

bool ImageEngineApi::insertObject(void *obj)
{
    m_AllObject.insert(obj, obj);
    return true;
}
bool ImageEngineApi::removeObject(void *obj)
{
    QMap<void *, void *>::iterator it;
    it = m_AllObject.find(obj);
    if (it != m_AllObject.end()) {
        m_AllObject.erase(it);
        return true;
    }
    return false;
}

bool ImageEngineApi::ifObjectExist(void *obj)
{
    return m_AllObject.contains(obj);
}

bool ImageEngineApi::removeImage(QStringList imagepathList)
{
    for (const auto &imagepath : imagepathList) {
        m_AllImageData.remove(imagepath);
    }
    return true;
}

bool ImageEngineApi::insertImage(const QString &imagepath, const QString &remainDay, bool reLoadIsvideo)
{
    bool bexsit = m_AllImageData.contains(imagepath);
    if (bexsit && remainDay.isEmpty()) {
        return false;
    }

    DBImgInfo data;
    if (bexsit) {
        data = m_AllImageData[imagepath];
    }

    if (reLoadIsvideo) {
        bool isVideo = utils::base::isVideo(imagepath);
        if (isVideo) {
            data.itemType = ItemTypeVideo;
        } else {
            data.itemType = ItemTypePic;
        }
    }
    addImageData(imagepath, data);
    return true;
}

bool ImageEngineApi::getImageData(QString imagepath, DBImgInfo &data)
{
    QMap<QString, DBImgInfo>::iterator it;
    it = m_AllImageData.find(imagepath);
    if (it == m_AllImageData.end()) {
        return false;
    }
    data = it.value();
    return true;
}

void ImageEngineApi::sltImageFilesImported(void *imgobject, QStringList &filelist)
{
    if (nullptr != imgobject && ifObjectExist(imgobject)) {
        static_cast<ImageMountImportPathsObject *>(imgobject)->imageMountImported(filelist);
    }
}

void ImageEngineApi::sigImageBackLoaded(const QString &path, const DBImgInfo &data)
{
    addImageData(path, data);
}

void ImageEngineApi::slt80ImgInfosReady(QVector<DBImgInfo> ImageDatas)
{
    m_AllImageDataVector = ImageDatas;
    for (int i = 0; i < ImageDatas.size(); i++) {
        DBImgInfo data = ImageDatas.at(i);
        addImageData(data.filePath, data);
    }
    emit sigLoadFirstPageThumbnailsToView();
}

bool ImageEngineApi::ImportImagesFromUrlList(QList<QUrl> files, const QString &albumname, int UID, ImageEngineImportObject *obj, bool bdialogselect, AlbumDBType dbType)
{
    emit dApp->signalM->popupWaitDialog(QObject::tr("Importing..."));
    ImportImagesThread *imagethread = new ImportImagesThread;
    imagethread->setData(files, albumname, UID, obj, bdialogselect, dbType);
    obj->addThread(imagethread);
#ifdef NOGLOBAL
    m_qtpool.start(imagethread);
#else
    QThreadPool::globalInstance()->start(imagethread);
#endif
    return true;
}

bool ImageEngineApi::ImportImagesFromFileList(QStringList files, const QString &albumname, int UID, ImageEngineImportObject *obj, bool bdialogselect, AlbumDBType dbType)
{
    emit dApp->signalM->popupWaitDialog(QObject::tr("Importing..."));
    ImportImagesThread *imagethread = new ImportImagesThread;
    imagethread->setData(files, albumname, UID, obj, bdialogselect, dbType);
    obj->addThread(imagethread);
#ifdef NOGLOBAL
    m_qtpool.start(imagethread);
#else
    QThreadPool::globalInstance()->start(imagethread);
#endif
    return true;
}

bool ImageEngineApi::removeImageFromAutoImport(const QStringList &files)
{
    //直接删除图片
    DBManager::instance()->removeImgInfos(files);

    return true;
}

void ImageEngineApi::loadFirstPageThumbnails(int num, bool clearCache)
{
    qDebug() << __FUNCTION__ << "---";

    m_FirstPageScreen = num;
    if (clearCache) {
        m_AllImageDataVector.clear();
    }
    thumbnailLoadThread(num);
    QStringList list;
    QSqlQuery query;
    query.setForwardOnly(true);
    if (!query.exec(QString("SELECT FilePath, FileName, Dir, Time, ChangeTime, ImportTime, FileType FROM ImageTable3 order by Time desc limit %1").arg(QString::number(num)))) {
        qDebug() << "------" << __FUNCTION__ <<  query.lastError();
        return;
    } else {
        int count = 0;
        while (query.next()) {
            DBImgInfo info;
            info.filePath = query.value(0).toString();
            if (count < num) {
                list << info.filePath;
                count++;
            }
            info.time = utils::base::stringToDateTime(query.value(3).toString());
            info.changeTime = QDateTime::fromString(query.value(4).toString(), DATETIME_FORMAT_DATABASE);
            info.importTime = QDateTime::fromString(query.value(5).toString(), DATETIME_FORMAT_DATABASE);
            info.itemType = static_cast<ItemType>(query.value(6).toInt());
            addImageData(info.filePath, info);
            m_AllImageDataVector.append(info);
        }
    }

    qDebug() << "------" << __FUNCTION__ << "" << m_AllImageDataVector.size();
    m_firstPageIsLoaded = true;
    ImageDataService::instance()->readThumbnailByPaths(list);

    emit sigLoadFirstPageThumbnailsToView();
}

void ImageEngineApi::thumbnailLoadThread(int num)
{
    Q_UNUSED(num)
    if (m_worker != nullptr) {
        return;
    }
    QThread *workerThread = new QThread(this);
    m_worker = new DBandImgOperate(workerThread);

    m_worker->moveToThread(workerThread);
    //开始录制
    connect(this, &ImageEngineApi::sigLoadThumbnailsByNum, m_worker, &DBandImgOperate::sltLoadThumbnailByNum);
//    connect(this, &ImageEngineApi::sigLoadThumbnailIMG, m_worker, &DBandImgOperate::loadOneImg);
    //加载设备中文件列表
    connect(this, &ImageEngineApi::sigLoadMountFileList, m_worker, &DBandImgOperate::sltLoadMountFileList);
    //同步设备卸载
    connect(this, &ImageEngineApi::sigDeciveUnMount, m_worker, &DBandImgOperate::sltDeciveUnMount);
    //旋转一张图片
    connect(this, &ImageEngineApi::sigRotateImageFile, m_worker, &DBandImgOperate::rotateImageFile);

    //收到获取全部照片信息成功信号
    connect(m_worker, &DBandImgOperate::sig80ImgInfosReady, this, &ImageEngineApi::slt80ImgInfosReady);
    connect(m_worker, &DBandImgOperate::sigOneImgReady, this, &ImageEngineApi::sigOneImgReady);
    //加载设备中文件列表完成，发送到主线程
    connect(m_worker, &DBandImgOperate::sigMountFileListLoadReady, this, &ImageEngineApi::sigMountFileListLoadReady);
    workerThread->start();
}

void ImageEngineApi::setThreadShouldStop()
{
    if (nullptr != m_worker) {
        m_worker->setThreadShouldStop();
    }
}

void ImageEngineApi::cleanUpTrash(const DBImgInfoList &list)
{
    RefreshTrashThread *imagethread = new RefreshTrashThread();
    imagethread->setData(list);
    QThreadPool::globalInstance()->start(imagethread);
}

bool ImageEngineApi::reloadAfterFilterUnExistImage()
{
    ImageLoadFromDBThread *imagethread = new ImageLoadFromDBThread();
#ifdef NOGLOBAL
    m_qtpool.start(imagethread);
#else
    QThreadPool::globalInstance()->start(imagethread);
#endif
    return true;
}

int ImageEngineApi::getAllImageDataCount()
{
    QMutexLocker locker(&m_dataMutex);
    return m_AllImageData.size();
}

void ImageEngineApi::addImageData(QString path, DBImgInfo data)
{
    QMutexLocker locker(&m_dataMutex);
    m_AllImageData[path] = data;
}

void ImageEngineApi::clearAllImageData()
{
    QMutexLocker locker(&m_dataMutex);
    m_AllImageData.clear();
}

bool ImageEngineApi::isItemLoadedFromDB(QString path)
{
    QMutexLocker locker(&m_dataMutex);
    return m_AllImageData.contains(path);
}

bool ImageEngineApi::importImageFilesFromMount(QString albumname, int UID, QStringList paths, ImageMountImportPathsObject *obj)
{
    emit dApp->signalM->popupWaitDialog(QObject::tr("Importing..."));
    ImageImportFilesFromMountThread *imagethread = new ImageImportFilesFromMountThread;
    connect(imagethread, &ImageImportFilesFromMountThread::sigImageFilesImported, this, &ImageEngineApi::sltImageFilesImported);
//    if (albumname == tr("Gallery")) {
//        albumname = "";
//    }
    imagethread->setData(albumname, UID, paths, obj);
    obj->addThread(imagethread);
#ifdef NOGLOBAL
    m_qtpool.start(imagethread);
#else
    QThreadPool::globalInstance()->start(imagethread);
#endif
    return true;
}
bool ImageEngineApi::moveImagesToTrash(QStringList files, bool typetrash, bool bneedprogress)
{
    if (files.size() == 0) {
        return false;
    }

    //非最近删除进来的，需要剔除存在且没有权限的部分
    if (!typetrash) {
        auto iter = std::remove_if(files.begin(), files.end(), [](const QString & eachFile) {
            QFileInfo info(eachFile);
            return !QFile::permissions(eachFile).testFlag(QFile::WriteUser) && info.exists();
        });
        files.erase(iter, files.end());
        if (files.isEmpty()) {
            return true;
        }
    }

    emit dApp->signalM->popupWaitDialog(tr("Deleting..."), bneedprogress); //author : jia.dong
    if (typetrash)  //如果为回收站删除，则删除内存数据
        removeImage(files);
    ImageMoveImagesToTrashThread *imagethread = new ImageMoveImagesToTrashThread;
    imagethread->setData(files, typetrash);
#ifdef NOGLOBAL
    m_qtpool.start(imagethread);
#else
    QThreadPool::globalInstance()->start(imagethread);
#endif
    return true;
}
bool ImageEngineApi::recoveryImagesFromTrash(QStringList files)
{
    emit dApp->signalM->popupWaitDialog(tr("Restoring..."), false);
    ImageRecoveryImagesFromTrashThread *imagethread = new ImageRecoveryImagesFromTrashThread;
    imagethread->setData(files);
#ifdef NOGLOBAL
    m_qtpool.start(imagethread);
#else
    QThreadPool::globalInstance()->start(imagethread);
#endif
    return true;
}
