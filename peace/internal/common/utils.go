package common

import (
	"fmt"
	"net"
	"strings"
	"time"
)

func GetAuthorizationToken(authHeader string) (string, error) {
	parts := strings.SplitN(authHeader, " ", 2)
	if !(len(parts) == 2 && parts[0] == "Bearer") {
		return "", NewErrNo(TOKEN_INVALID)
	}
	return parts[1], nil
}

func IsValidIP(ip string) bool {
	return net.ParseIP(ip) != nil
}

func IsValidPort(port int) bool {
	return port >= 1 && port <= 65535
}

func IsServerOnline(ip string, port int) bool {
	addr := fmt.Sprintf("%s:%d", ip, port)
	conn, err := net.DialTimeout("tcp", addr, 2*time.Second)
	if err != nil {
		return false
	}
	conn.Close()
	return true
}
