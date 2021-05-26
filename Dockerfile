FROM ubuntu:21.04 AS build
WORKDIR /iqdb
RUN \
  apt-get update && \
  apt-get install --yes --no-install-recommends build-essential g++ libgd-dev libjpeg-dev libpng-dev
COPY *.h *.cpp Makefile ./
RUN make -j "$(nproc)"

FROM ubuntu:21.04
RUN \
  apt-get update && \
  apt-get install --yes --no-install-recommends libgd3 libjpeg-turbo8 libpng16-16
COPY --from=build /iqdb/iqdb /usr/local/bin

ENTRYPOINT ["iqdb"]
CMD ["listen", "8001"]
