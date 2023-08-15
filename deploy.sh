#!/bin/sh

NAME=matrix
HOST=${1:-${NAME}}
INSTALL_DIR=/home/pi/$NAME

echo "Stop"
ssh $HOST sudo systemctl stop matrix.service

echo "Deploy"
rsync -av -e ssh --exclude=".*" --exclude="deploy.sh" ./ $HOST:$INSTALL_DIR/
ssh $HOST chmod +x $INSTALL_DIR

ssh $HOST cp $INSTALL_DIR/rpi-rgb-led-matrix/fonts/4x6.bdf $INSTALL_DIR
ssh $HOST chmod 777 $INSTALL_DIR/4x6.bdf

ssh $HOST mkdir -p $INSTALL_DIR/images
ssh $HOST chmod +x $INSTALL_DIR/images

echo "Compile"
ssh $HOST "cd $INSTALL_DIR; make"

echo "Start"
ssh $HOST sudo cp $INSTALL_DIR/matrix.service /lib/systemd/system/
ssh $HOST sudo systemctl daemon-reload
ssh $HOST sudo systemctl enable matrix.service
ssh $HOST sudo systemctl start matrix.service
