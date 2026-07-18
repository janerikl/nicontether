#include <QApplication>

#include "ui/MainWindow.h"
#include "edit/RetouchWindow.h"
#include "edit/RetouchTab.h"
#include <QElapsedTimer>
#include <cstdio>
#include <cstring>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("NikonTether");
    app.setOrganizationName("NikonTether");

    if (argc >= 3 && std::strcmp(argv[1], "--undotest") == 0) {
        RetouchWindow w;
        w.openPhoto(argv[2]);
        auto *tab = w.findChild<RetouchTab *>();
        QElapsedTimer t; t.start();
        while (!tab->isReady() && t.elapsed() < 15000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        auto pump = [](int ms){ QElapsedTimer e; e.start();
            while (e.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); };

        printf("start canUndo=%d canRedo=%d\n", tab->canUndo(), tab->canRedo());

        Adjustments a1 = tab->adjustments(); a1.brightness = 30;
        tab->setAdjustments(a1); pump(400); // commit
        printf("after brightness=30: bri=%d canUndo=%d\n",
               tab->adjustments().brightness, tab->canUndo());

        Adjustments a2 = tab->adjustments(); a2.contrast = -20;
        tab->setAdjustments(a2); pump(400);
        printf("after contrast=-20: con=%d canUndo=%d\n",
               tab->adjustments().contrast, tab->canUndo());

        tab->undo(); pump(50);
        printf("undo -> bri=%d con=%d canRedo=%d\n",
               tab->adjustments().brightness, tab->adjustments().contrast, tab->canRedo());
        tab->undo(); pump(50);
        printf("undo -> bri=%d con=%d canUndo=%d\n",
               tab->adjustments().brightness, tab->adjustments().contrast, tab->canUndo());
        tab->redo(); pump(50);
        printf("redo -> bri=%d con=%d\n",
               tab->adjustments().brightness, tab->adjustments().contrast);
        tab->redo(); pump(50);
        printf("redo -> bri=%d con=%d canRedo=%d\n",
               tab->adjustments().brightness, tab->adjustments().contrast, tab->canRedo());

        // Geometry: rotate goes through setAdjustments too.
        Adjustments a3 = tab->adjustments(); a3.rotationQuadrants = 1;
        tab->setAdjustments(a3); pump(400);
        printf("after rotate90: rot=%d canUndo=%d\n",
               tab->adjustments().rotationQuadrants, tab->canUndo());

        tab->undo(); pump(50);
        printf("undo -> rot=%d bri=%d canRedo=%d\n",
               tab->adjustments().rotationQuadrants, tab->adjustments().brightness, tab->canRedo());

        // Crop: exercise the direct-field + markEdited() path (resetCrop),
        // distinct from setAdjustments-driven edits above.
        Adjustments withCrop = tab->adjustments();
        withCrop.cropRect = QRect(0, 0, 100, 100);
        tab->setAdjustments(withCrop); pump(400);
        printf("after crop set: crop=%dx%d canUndo=%d\n",
               tab->adjustments().cropRect.width(), tab->adjustments().cropRect.height(),
               tab->canUndo());

        tab->resetCrop(); pump(400);
        printf("after resetCrop: crop=%s canUndo=%d\n",
               tab->adjustments().cropRect.isNull() ? "null" : "set", tab->canUndo());

        tab->undo(); pump(50);
        printf("undo resetCrop -> crop=%s canRedo=%d\n",
               tab->adjustments().cropRect.isNull() ? "null" : "set", tab->canRedo());
        return 0;
    }

    MainWindow window;
    window.show();
    return app.exec();
}
