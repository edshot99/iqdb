FROM ubuntu:20.10 AS build
WORKDIR /iqdb
RUN \
  apt-get update && \
  apt-get install --yes --no-install-recommends build-essential g++ libgd-dev libjpeg-dev libpng-dev
COPY . ./
RUN make -j "$(nproc)"

FROM ubuntu:20.10
RUN \
  apt-get update && \
  apt-get install --yes --no-install-recommends libgd3 libjpeg-turbo8 libpng16-16
COPY --from=build /iqdb/iqdb /iqdb/maint/iqdb-status /usr/local/bin/

ENTRYPOINT ["iqdb"]
CMD ["listen", "0.0.0.0:5588"]
