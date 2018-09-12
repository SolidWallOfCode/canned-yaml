if [ ! -d build-env ]; then
virtualenv -p python2 build-env
. build-env/bin/activate
pip install pip --upgrade
pip install scons
# used pinned version to be safe
pip install git+https://bitbucket.org/sconsparts/parts.git@v0.11.1
else
. build-env/bin/activate
fi
