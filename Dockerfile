FROM emscripten/emsdk:4.0.8

RUN dpkg --add-architecture i386

RUN apt update && apt upgrade -y && apt -y --no-install-recommends install aptitude
RUN aptitude -y --without-recommends install git ca-certificates build-essential gcc-multilib g++-multilib libsdl2-dev:i386 libfreetype-dev:i386 libopus-dev:i386 libbz2-dev:i386 libvorbis-dev:i386 libopusfile-dev:i386 libogg-dev:i386
ENV PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig

WORKDIR /xash3d-fwgs
COPY . .
ENV EMCC_CFLAGS="-s USE_SDL=2"
RUN emconfigure ./waf configure --enable-stbtt && \
	emmake ./waf build

RUN cp build/filesystem/filesystem_stdio.so build/engine/filesystem_stdio
RUN cp build/ref/gl/libref_gl.so build/engine/ref_gl.so
RUN cp build/ref/soft/libref_soft.so build/engine/ref_soft.so
RUN cp build/3rdparty/mainui/libmenu.so build/engine/menu
COPY web/valve.zip build/engine/valve.zip
RUN sed -e '/var Module = typeof Module != '\''undefined'\'' ? Module : {};/{r web/head.js' -e 'd}' -i build/engine/index.js
RUN sed -e 's/run();//g' -i build/engine/index.js
RUN sed -e '/preInit();/{r web/init.js' -e 'd}' -i build/engine/index.js
RUN sed -e 's/async type="text\/javascript"/defer type="module"/' -i build/engine/index.html

CMD ["emrun", "--no_browser", "--port", "8080", "./build/engine"]
