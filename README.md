Huntercoin
==========

Human-mineable crypto currency / decentralized game

www.huntercoin.org

latest Windows build:

huntercoin-qt-v140-win32-20170304.zip, 14.6 MB

https://mega.nz/#!rAdgxKIS!I3zoMMrGptIPjzFxzlMccAN_d3Ov_XAgH0qw7_iO598

To build on a new Ubuntu 16.04 or Linux Mint 18

    sudo apt-get install libboost-chrono-dev libboost-date-time-dev libboost-filesystem-dev libboost-program-options-dev libboost-serialization-dev libboost-system-dev libboost-thread-dev
    sudo apt-get install libboost-dev git qt4-qmake libqt4-dev build-essential qt4-linguist-tools libssl-dev
    sudo apt-get install libdb++-dev

if Qt Creator is installed after this, open huntercoin-qt.pro, and Build | Build project huntercoin-qt, otherwise

    qmake
    make

Fast blockchain download
========================

http://forum.huntercoin.org/index.php/topic,24070.0.html

