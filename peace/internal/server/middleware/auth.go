package middleware

import (
	"fmt"
	"pace/internal/common"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/golang-jwt/jwt/v5"
)

type Claims struct {
	Role string `json:"role"`
	jwt.RegisteredClaims
}

func GenerateJWT(userRole string) (string, error) {
	expirationTime := time.Now().Add(common.JWTExpire)
	claims := &Claims{
		Role: userRole,
		RegisteredClaims: jwt.RegisteredClaims{
			ExpiresAt: jwt.NewNumericDate(expirationTime),
		},
	}
	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	return token.SignedString([]byte(common.JWTKey))
}

func JWTAuthMiddleware() gin.HandlerFunc {
	return func(c *gin.Context) {
		tokenString, err := common.GetAuthorizationToken(c.GetHeader("Authorization"))
		if err != nil {
			common.Error(c, common.NewErrNo(common.TOKEN_INVALID))
			c.Abort()
			return
		}

		claims := &Claims{}
		token, err := jwt.ParseWithClaims(tokenString, claims, func(t *jwt.Token) (interface{}, error) {
			return []byte(common.JWTKey), nil
		})

		if err != nil || !token.Valid {
			common.Error(c, common.NewErrNo(common.TOKEN_INVALID))
			c.Abort()
			return
		}

		if claims.ExpiresAt.Time.Before(time.Now().Add(common.JWTNewExpire)) {
			newToken, err := GenerateJWT(claims.Role)
			if err != nil {
				common.Error(c, common.NewErrNo(common.TOKEN_INVALID))
				c.Abort()
				return
			}
			c.Header("Authorization", "Bearer "+newToken)
		}
		fmt.Println("User role:", claims.Role)
		c.Set("userRole", claims.Role)
		c.Next()
	}
}
