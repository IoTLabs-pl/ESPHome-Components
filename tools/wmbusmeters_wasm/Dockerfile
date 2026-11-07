# From the repo root, with the workflow's env: docker build -t wmbusmeters_wasm tools/wmbusmeters_wasm &&
# docker run --rm -e ARDUINOJSON_VERSION -e MBEDTLS_VERSION -u "$(id -u):$(id -g)" -v "$PWD:$PWD" -w "$PWD/tools/wmbusmeters_wasm" wmbusmeters_wasm
FROM emscripten/emsdk:6.0.13

ARG ESPHOME_VERSION
RUN python3 -m venv --without-pip /opt/esphome \
 && python3 -m pip --python /opt/esphome/bin/python install --no-cache-dir "esphome${ESPHOME_VERSION:+==$ESPHOME_VERSION}"
ENV PATH=/opt/esphome/bin:$PATH

CMD emcmake cmake -S . -B build && cmake --build build -j"$(nproc)"
