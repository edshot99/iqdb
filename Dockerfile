FROM ubuntu:20.10 AS build
WORKDIR /iqdb
RUN \
  apt-get update && \
  apt-get install --yes --no-install-recommends build-essential g++ libgd-dev libjpeg-dev libpng-dev
COPY . ./
RUN make -j "$(nproc)"

FROM ubuntu:20.10
WORKDIR /iqdb
RUN \
  apt-get update && \
  apt-get install --yes --no-install-recommends libgd3 libjpeg-turbo8 libpng16-16
COPY --from=build /iqdb ./
RUN ln -t /usr/local/bin ./iqdb ./test-iqdb

EXPOSE 5588
ENTRYPOINT ["iqdb"]
CMD ["http", "0.0.0.0", "5588", "iqdb.db"]
