# 生成私钥
openssl genrsa -out server.key 2048

# 生成自签名证书（应用配置文件）
openssl req -new -x509 -days 365 -key server.key -out server.crt -config ssl.conf