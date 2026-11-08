FROM alpine:3.20

RUN apk add --no-cache build-base

WORKDIR /app
COPY include/ ./include/
COPY src/ ./src/

RUN mkdir -p bin && \
    gcc -Wall -Wextra -O2 src/observer.c -o bin/observer -lpthread && \
    gcc -Wall -Wextra -O2 src/node.c -o bin/node -lpthread && \
    gcc -Wall -Wextra -O2 src/client.c -o bin/client -lpthread