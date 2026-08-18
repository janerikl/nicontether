#include <QApplication>
#include <QPixmap>

#include "edit/RetouchWindow.h"
#include "edit/RetouchTab.h"
#include "ui/AppIcon.h"
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cstdio>
#include <cstring>

namespace {
// One-time migration from the app's former name ("NikonTether") so a
// rename doesn't silently orphan a user's existing window layout/recent-
// files/camera-preference settings under ~/.config/NikonTether. Copies
// (doesn't move) the old config dir the first time the new one is missing.
void migrateLegacySettings() {
    QDir oldDir(QDir::homePath() + "/.config/NikonTether");
    QDir newDir(QDir::homePath() + "/.config/Photonloom");
    if (!oldDir.exists() || newDir.exists()) return;
    QDir().mkpath(newDir.absolutePath());
    for (const QString &name : oldDir.entryList(QDir::Files))
        QFile::copy(oldDir.filePath(name), newDir.filePath(name));
}
} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    migrateLegacySettings();
    app.setApplicationName("Photonloom");
    app.setOrganizationName("Photonloom");
    app.setWindowIcon(makeShutterIcon());

    if (argc >= 2 && std::strcmp(argv[1], "--export-icon") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: photonloom --export-icon <path.png>\n");
            return 2;
        }
        QPixmap pm = makeShutterIcon().pixmap(256, 256);
        if (!pm.save(argv[2], "PNG")) {
            fprintf(stderr, "error: could not write icon to %s\n", argv[2]);
            return 1;
        }
        return 0;
    }

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

    if (argc >= 4 && std::strcmp(argv[1], "--layertest") == 0) {
        QString photoPath = argv[2];
        QString layerPath = argv[3];
        auto pump = [](int ms){ QElapsedTimer e; e.start();
            while (e.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); };

        {
            RetouchWindow w;
            w.openPhoto(photoPath);
            auto *tab = w.findChild<RetouchTab *>();
            QElapsedTimer t; t.start();
            while (!tab->isReady() && t.elapsed() < 15000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            printf("[session1] base ready\n");

            tab->addImageLayer(layerPath);
            pump(3000); // let async layer decode land
            const auto &masks1 = tab->adjustments().masks;
            printf("[session1] masks=%d cache_null=%d missing=%d\n",
                   masks1.size(),
                   masks1.isEmpty() ? -1 : masks1[0].sourceImageCache.isNull(),
                   masks1.isEmpty() ? -1 : masks1[0].sourceMissing);

            // Mimic the reported repro: touch a tone slider shortly after adding
            // the layer, before/around when the async decode lands.
            Adjustments a = tab->adjustments();
            a.brightness = 10;
            tab->setAdjustments(a);
            pump(500);
            const auto &masks1b = tab->adjustments().masks;
            printf("[session1] after slider touch: cache_null=%d\n",
                   masks1b.isEmpty() ? -1 : masks1b[0].sourceImageCache.isNull());

            tab->saveEdits();
            printf("[session1] saved\n");
        }

        printf("[session2] simulating restart (new RetouchTab, same path)\n");
        {
            RetouchWindow w2;
            w2.openPhoto(photoPath);
            auto *tab2 = w2.findChild<RetouchTab *>();
            QElapsedTimer t2; t2.start();
            while (!tab2->isReady() && t2.elapsed() < 15000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            pump(3000); // let async layer-cache decode land after reload
            const auto &masks2 = tab2->adjustments().masks;
            printf("[session2] masks=%d cache_null=%d missing=%d visible=%d opacity=%f blend=%d type=%d sourcePath=%s\n",
                   masks2.size(),
                   masks2.isEmpty() ? -1 : masks2[0].sourceImageCache.isNull(),
                   masks2.isEmpty() ? -1 : masks2[0].sourceMissing,
                   masks2.isEmpty() ? -1 : masks2[0].visible,
                   masks2.isEmpty() ? -1.0 : masks2[0].opacity,
                   masks2.isEmpty() ? -1 : int(masks2[0].blend),
                   masks2.isEmpty() ? -1 : int(masks2[0].type),
                   masks2.isEmpty() ? "" : masks2[0].sourceImagePath.toUtf8().constData());

            QImage withLayer = tab2->previewImage();
            withLayer.save("/tmp/claude-1000/-home-janel-Development-imgcapture/90e91887-d636-4e98-98fb-228d678bd2eb/scratchpad/layertest/session2_with_layer.png");

            // Toggle the layer off to get an unambiguous A/B comparison.
            Adjustments off = tab2->adjustments();
            off.masks[0].visible = false;
            tab2->setAdjustments(off);
            pump(1000);
            QImage withoutLayer = tab2->previewImage();
            withoutLayer.save("/tmp/claude-1000/-home-janel-Development-imgcapture/90e91887-d636-4e98-98fb-228d678bd2eb/scratchpad/layertest/session2_without_layer.png");

            long diff = 0;
            if (withLayer.size() == withoutLayer.size() && !withLayer.isNull()) {
                QImage a = withLayer.convertToFormat(QImage::Format_RGB888);
                QImage b = withoutLayer.convertToFormat(QImage::Format_RGB888);
                for (int y = 0; y < a.height(); y += 7)
                    for (int x = 0; x < a.width(); x += 7)
                        diff += std::abs(int(a.scanLine(y)[x*3]) - int(b.scanLine(y)[x*3]));
            }
            printf("[session2] pixel diff (with vs without layer) = %ld\n", diff);
        }
        return 0;
    }

    RetouchWindow window;
    window.show();

    // Launched via "Open With…"/double-click (desktop file's %f/%u), or a
    // plain `photonloom <file>` from the shell: open it directly instead of
    // coming up blank. argv[1] is never one of the debug-flag branches above
    // since those all `return` before reaching here.
    if (argc >= 2 && QFileInfo::exists(argv[1]))
        window.openPhoto(argv[1]);

    return app.exec();
}
