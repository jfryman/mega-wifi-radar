# mega_wifi_radar relay: static Go binary on distroless (CA certs included for HTTPS upstream).
#   scripts/relay build && scripts/relay run      (or: make relay-run)
# Cross-compiles on the build host's platform, so multi-arch builds don't emulate the Go toolchain.
FROM --platform=$BUILDPLATFORM docker.io/library/golang:1.26-bookworm AS build
ARG TARGETOS
ARG TARGETARCH
ARG VERSION=dev
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY *.go *.csv ./
COPY testdata ./testdata
RUN go vet ./... && go test ./... \
 && CGO_ENABLED=0 GOOS=${TARGETOS:-linux} GOARCH=${TARGETARCH:-$(go env GOARCH)} go build -trimpath -ldflags="-s -w -X main.version=${VERSION}" -o /relay . \
 && mkdir -p /data

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /relay /relay
# the logbook's SQLite file (RELAY_DB=/data/relay.db), on a volume; writable by the nonroot user
COPY --from=build --chown=65532:65532 /data /data
EXPOSE 8080
USER nonroot
ENTRYPOINT ["/relay"]
CMD ["-listen", ":8080"]
