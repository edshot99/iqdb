FROM alpine:3.20 as builder

RUN apk --no-cache add sqlite-dev cmake git build-base

WORKDIR /iqdb
COPY . .
RUN make release

FROM alpine:3.20

RUN apk --no-cache add sqlite-libs binutils

COPY --from=builder /iqdb/build/release/src/iqdb /usr/local/bin/

CMD ["iqdb", "http", "0.0.0.0", "5588", "/iqdb/data.db"]
