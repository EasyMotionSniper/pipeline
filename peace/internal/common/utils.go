package common

import (
	"strings"
)

func GetAuthorizationToken(authHeader string) (string, error) {
	parts := strings.SplitN(authHeader, " ", 2)
	if !(len(parts) == 2 && parts[0] == "Bearer") {
		return "", NewErrNo(TokenInvalid)
	}
	return parts[1], nil
}
