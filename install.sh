#!/bin/bash

sudo add-apt-repository -y ppa:chris-lea/node.js
sudo apt-get update
sudo apt-get install -y \
  cmake make g++ \
  libtbb-dev \
  libprotobuf-dev \
  protobuf-compiler \
  python-protobuf \
  libhdf5-serial-dev \
  libssl-dev \
  libzmq-dev \
  python-pip \
  virtualenvwrapper \
  python-tables \
  graphviz \
  gdb \
  p7zip-full \
  nodejs \
  #

if env | grep -q ^VIRTUAL_ENV=
then
	echo "Installing in $VIRTUAL_ENV"
else
	echo "Making new virtualenv"
	mkvirtualenv --system-site-packages pomagma
	deactivate
	workon pomagma
fi
pip install -r requirements.txt
pip install -e .
. $VIRTUAL_ENV/bin/activate

npm install

make all
