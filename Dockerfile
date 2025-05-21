FROM emscripten/emsdk:4.0.9 AS hlsdk

RUN dpkg --add-architecture i386
RUN apt update && apt upgrade -y && apt -y --no-install-recommends install aptitude
RUN aptitude -y --without-recommends install cmake build-essential gcc-multilib g++-multilib libsdl2-dev:i386

WORKDIR /hlsdk-portable
COPY hlsdk-portable .

RUN emconfigure ./waf configure -T release
RUN emmake ./waf


FROM emscripten/emsdk:4.0.9 AS engine

RUN dpkg --add-architecture i386
RUN apt update && apt upgrade -y && apt -y --no-install-recommends install aptitude
RUN aptitude -y --without-recommends install git ca-certificates build-essential gcc-multilib g++-multilib libsdl2-dev:i386 libfreetype-dev:i386 libopus-dev:i386 libbz2-dev:i386 libvorbis-dev:i386 libopusfile-dev:i386 libogg-dev:i386
ENV PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig

WORKDIR /xash3d-fwgs
COPY . .
ENV EMCC_CFLAGS="-s USE_SDL=2"
RUN emconfigure ./waf configure --enable-stbtt && \
	emmake ./waf build

RUN sed -e '/var Module = typeof Module != '\''undefined'\'' ? Module : {};/{r web/head.js' -e 'd}' -i build/engine/index.js
RUN sed -e '/filename = PATH.normalize(filename);/{r web/filename.js' -e 'd}' -i build/engine/index.js
RUN sed -e 's/run();//g' -i build/engine/index.js
RUN sed -e '/preInit();/{r web/init.js' -e 'd}' -i build/engine/index.js
RUN sed -e 's/async type="text\/javascript"/defer type="module"/' -i build/engine/index.html


FROM nginx:alpine3.21 AS server

COPY --from=hlsdk /hlsdk-portable/build/dlls/hl_emscripten_javascript.so /usr/share/nginx/html/server.wasm
COPY --from=hlsdk /hlsdk-portable/build/cl_dll/client_emscripten_javascript.so /usr/share/nginx/html/client.wasm
COPY --from=engine /xash3d-fwgs/build/engine/index.html /usr/share/nginx/html/index.html
COPY --from=engine /xash3d-fwgs/build/engine/index.js /usr/share/nginx/html/index.js
COPY --from=engine /xash3d-fwgs/build/engine/index.wasm /usr/share/nginx/html/index.wasm
COPY --from=engine /xash3d-fwgs/build/filesystem/filesystem_stdio.so /usr/share/nginx/html/filesystem_stdio
COPY --from=engine /xash3d-fwgs/build/ref/gl/libref_gl.so /usr/share/nginx/html/ref_gl.so
COPY --from=engine /xash3d-fwgs/build/ref/soft/libref_soft.so /usr/share/nginx/html/ref_soft.so
COPY --from=engine /xash3d-fwgs/build/3rdparty/mainui/libmenu.so /usr/share/nginx/html/menu
COPY web/valve.zip /usr/share/nginx/html/valve.zip

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]
