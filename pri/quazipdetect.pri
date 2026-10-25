# Copyright (c) 2021,2024 Fritzing GmbH

message("Using Fritzing quazip detect script.")

# We are currently using a quazip version from this PR:
# https://github.com/stachenov/quazip/pull/199
# Important:
# Quazip is looking for additional maintainers and code reviewers. While the
# library is # mostly feature complete, there are a couple of big ticket
# issues that need # work. Code review is welcome for all pull requests.
# If you are well versed in # Qt and C/C++ please start helping around
# and check https://github.com/stachenov/quazip/issues/185
# FLOATING NOTE: the quazip install is keyed by Qt MAJOR version only
# (quazip_qt6 / quazip_qt5), NOT by the full Qt patch version. Keying the path
# on $QT_VERSION (patch-precise, e.g. quazip-6.11.1-...) broke configure on
# every Qt patch bump, which is incompatible with tracking the newest Qt.
# The library itself is major-versioned (libquazip1-qt6.so), so major-keying is
# both correct and stable across patch/minor bumps.
QUAZIP_PATH=$$absolute_path($$PWD/../../quazip_qt$$QT_MAJOR_VERSION)
QUAZIP_LIB_PATH=$$QUAZIP_PATH/lib

SOURCES += \
	src/zlibdummy.c \

exists($$QUAZIP_PATH) {
		message("found quazip in $${QUAZIP_PATH}")
	} else {
		error("quazip could not be found at $$QUAZIP_PATH")
	}

INCLUDEPATH += $$QUAZIP_INCLUDE_PATH
LIBS += -L$$QUAZIP_LIB_PATH -lquazip1-qt$$QT_MAJOR_VERSION

unix {
	message("set rpath for quazip")
	QMAKE_RPATHDIR += $$QUAZIP_LIB_PATH
}

macx {
	LIBS += -lz
}
