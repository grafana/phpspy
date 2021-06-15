FROM php:7.3-fpm-alpine
RUN apk add --update alpine-sdk
# RUN git clone --recursive https://github.com/adsr/phpspy.git
COPY ./ phpspy
RUN cd phpspy && make CFLAGS="-DUSE_DIRECT"
