FROM ubuntu:20.04 as platform
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -y
RUN apt-get install -y build-essential php7.4-dev
COPY ./lib /phpspy
WORKDIR /phpspy

FROM platform as test1
RUN apt-get install -y libgtest-dev
RUN make
RUN make tests

FROM platform as test2
RUN apt-get install -y libgtest-dev
RUN make CFLAGS="-DUSE_DIRECT"
RUN make tests

FROM platform as release
RUN make CFLAGS="-DUSE_DIRECT"
