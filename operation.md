版本更新：https://gist.github.com/nikhita/432436d570b89cab172dcf2894465753

go mod init pace

go get -u github.com/gin-gonic/gin

go get -u gorm.io/gorm

go get -u gorm.io/driver/mysql

go mod tidy

CREATE DATABASE pace;


# cli
go get -u github.com/spf13/cobra@latest

go build -o pipeline



go get gopkg.in/yaml.v3

go get github.com/docker/docker/client@v25.0.12

go get -u github.com/hibiken/asynq

go get github.com/google/uuid