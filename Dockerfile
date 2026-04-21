FROM docker.io/library/rust:trixie

WORKDIR /opt/hayroll

COPY prerequisites.bash /opt/hayroll/
RUN ./prerequisites.bash --no-sudo --llvm-version 19

COPY . /opt/hayroll/
RUN ./build.bash
RUN cd build && ctest --output-on-failure
