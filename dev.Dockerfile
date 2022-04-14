FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt update -y && apt install -y git vim make gcc php-dev libgtest-dev

WORKDIR /phpspy/
COPY . /phpspy/
RUN make CFLAGS="-DUSE_DIRECT" tests

ENTRYPOINT ["/phpspy/run_pyroscope_api_tests.sh"]
