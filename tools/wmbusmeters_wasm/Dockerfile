FROM emscripten/emsdk:7.0.0

ARG ESPHOME_VERSION
RUN python3 -m venv --without-pip /opt/esphome \
 && python3 -m pip --python /opt/esphome/bin/python install --no-cache-dir "esphome${ESPHOME_VERSION:+==$ESPHOME_VERSION}" pytest
ENV PATH=/opt/esphome/bin:$PATH

CMD emcmake cmake -S . -B build && cmake --build build -j"$(nproc)" \
 && PATH="$PWD:$PATH" pytest -q -p no:cacheprovider ../../tests/wmbus/host/test_telegrams.py
