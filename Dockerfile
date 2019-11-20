FROM ubuntu:19.10 AS build
RUN apt-get update && apt-get install --yes --no-install-recommends g++ libgd-dev libjpeg-dev libpng-dev
COPY *.h *.cpp Makefile /tmp/
RUN make -C /tmp

FROM ubuntu:19.10
RUN apt-get update && apt-get install --yes --no-install-recommends libgd3 libjpeg-turbo8 libpng16-16
COPY --from=build /tmp/iqdb /usr/local/bin

ENTRYPOINT ["iqdb"]
CMD help
