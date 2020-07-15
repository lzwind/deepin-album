#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>
#include "application.h"
#include "mainwindow.h"
#include "allpicview.h"

#include <QTestEventList>

TEST(allpicview, ini)
{
    MainWindow *w = dApp->getMainWindow();
    AllPicView *a = w->m_pAllPicView;
    a->updatePicNum();
    for (int i = 0; i < 10; i++) {
        a->m_pStatusBar->m_pSlider->setValue(i);
    }
}

TEST(allpicview, open)
{
    MainWindow *w = dApp->getMainWindow();
    AllPicView *a = w->m_pAllPicView;
    QStringList testPathlist = a->getThumbnailListView()->getAllPaths();
    if (!testPathlist.isEmpty()) {
        emit a->getThumbnailListView()->menuOpenImage(testPathlist.first(), testPathlist, false);
    }

}