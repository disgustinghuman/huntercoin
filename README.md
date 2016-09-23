huntercoin
==========

Human-mineable crypto currency / decentralized game

www.huntercoin.org

latest binaries for Windows + Linux

huntercoin-betterQt-exp-binaries-20160922.zip, 31.7 MB

https://mega.nz/#!WVdixSzL!Jxkf-ijnnvBtSifVR0CQNoTutgyRzc6GyndJlPBqc_E

![hunters](images/hunters1.jpg)

To build on a new Ubuntu 16.04 or Linux Mint 18

    sudo apt-get install libboost-chrono-dev libboost-date-time-dev libboost-filesystem-dev libboost-program-options-dev libboost-serialization-dev libboost-system-dev libboost-thread-dev
    sudo apt-get install libboost-dev git qt4-qmake libqt4-dev build-essential qt4-linguist-tools libssl-dev
    sudo apt-get install libdb++-dev

if Qt Creator is installed after this, open huntercoin-qt.pro, and Build | Build project huntercoin-qt, otherwise

    qmake
    make

Advanced mode:

rename huntercoin-qt to huntercoin-qt-safemode and replace with huntercoin-qt from binary release

or uncomment this to compile in advanced mode:

https://github.com/wiggi/huntercoin/blob/betterQt-with-storage/src/gamestate.h#L17

Don't forget to copy game_sv4.dat to '.huntercoin'

