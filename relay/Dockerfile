# mega_wifi_radar relay: static Go binary on distroless (CA certs included for HTTPS upstream).
#   scripts/relay build && scripts/relay run      (or: make relay-run)
# Cross-compiles on the build host's platform, so multi-arch builds don't emulate the Go toolchain.
FROM --platform=$BUILDPLATFORM docker.io/library/golang:1.24-bookworm AS build
ARG TARGETOS
ARG TARGETARCH
WORKDIR /src
COPY go.mod ./
COPY *.go ./
COPY testdata ./testdata
RUN go vet ./... && go test ./... \
 && CGO_ENABLED=0 GOOS=${TARGETOS:-linux} GOARCH=${TARGETARCH:-$(go env GOARCH)} go build -trimpath -ldflags="-s -w" -o /relay .

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /relay /relay
EXPOSE 8080
USER nonroot
ENTRYPOINT ["/relay"]
CMD ["-listen", ":8080"]
