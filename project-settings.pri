QT       += core gui multimedia opengl
CONFIG += c++14

QMAKE_CXXFLAGS_DEBUG += -O0 -g3 -Wextra -Winit-self -Wshadow -Wnon-virtual-dtor -pedantic -Wfloat-equal -Wundef
QMAKE_CXXFLAGS_RELEASE += -g1

CONFIG(debug, debug|release) {
    DESTDIR = build/debug
} else {
    DESTDIR = build/release
}

CONFIG(coverage) {

QMAKE_CXXFLAGS += -fprofile-arcs -ftest-coverage
}

OBJECTS_DIR = $${DESTDIR}/obj
MOC_DIR = $${DESTDIR}/moc
RCC_DIR = $${DESTDIR}/rcc
UI_DIR = $${DESTDIR}/ui

CONFIG += link_pkgconfig
PKGCONFIG += libavutil libavformat libavcodec libavfilter libswscale libswresample
