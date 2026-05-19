FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY src/relay_server.c ./src/relay_server.c

RUN gcc -O2 -Wall -Wextra -o /app/soc_relay /app/src/relay_server.c

FROM debian:bookworm-slim

WORKDIR /app
COPY --from=build /app/soc_relay /app/soc_relay

ENV PORT=10000
EXPOSE 10000

CMD ["/app/soc_relay"]