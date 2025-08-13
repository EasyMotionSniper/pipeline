FROM docker.1ms.run/golang:1.24-alpine AS builder

ENV GOPROXY=https://goproxy.cn,direct \
    GO111MODULE=on
WORKDIR /app

COPY go.mod go.sum ./
RUN go mod download

COPY . .

RUN CGO_ENABLED=0 GOOS=linux go build -o backend ./cmd/server/main.go
RUN CGO_ENABLED=0 GOOS=linux go build -o task_executor ./cmd/task_executor/main.go

FROM docker.1ms.run/alpine:3.17

COPY --from=builder /app/backend /usr/local/bin/backend
COPY --from=builder /app/task_executor /usr/local/bin/task_executor

EXPOSE 8080

