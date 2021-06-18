FROM ubuntu:20.10 AS build
WORKDIR /iqdb
RUN \
  apt-get update && \
  apt-get install --yes --no-install-recommends \
    ca-certificates build-essential cmake git python3 libgd-dev libsqlite3-dev
COPY . ./
RUN make release

FROM ubuntu:20.10
WORKDIR /iqdb
RUN \
  apt-get update && \
  apt-get install --yes --no-install-recommends libgd3 libsqlite3-0 sqlite3
COPY --from=build /iqdb/build/release/src/iqdb /usr/local/bin/

EXPOSE 5588
ENTRYPOINT ["iqdb"]
CMD ["http", "0.0.0.0", "5588", "iqdb.db"]
