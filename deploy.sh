#!/bin/sh

NAME=matrix
HOST=${1:-${NAME}}
INSTALL_DIR=/home/pi/$NAME

echo "Stop"
ssh $HOST sudo systemctl stop matrix.service

echo "Deploy"
rsync -av -e ssh --exclude=".git" ./ $HOST:$INSTALL_DIR/
ssh $HOST sudo chmod +x $INSTALL_DIR

ssh $HOST sudo cp $INSTALL_DIR/rpi-rgb-led-matrix/fonts/5x8.bdf $INSTALL_DIR
ssh $HOST sudo chmod 777 $INSTALL_DIR/5x8.bdf

ssh $HOST sudo mkdir -p $INSTALL_DIR/images
ssh $HOST sudo chmod +x $INSTALL_DIR/images

echo "Compile"
ssh $HOST "cd $INSTALL_DIR; make"

echo "Start"
ssh $HOST sudo cp $INSTALL_DIR/matrix.service /lib/systemd/system/
ssh $HOST sudo systemctl daemon-reload
ssh $HOST sudo systemctl enable matrix.service
ssh $HOST sudo systemctl start matrix.service
