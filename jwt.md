

## 核心功能说明

| 功能 | 说明 |
|------|------|
| **JWT 登录** | 用户提交用户名和密码后，服务器验证通过后生成 JWT 令牌并返回。 |
| **每次请求刷新令牌** | 每次请求成功后，服务器生成新的 JWT 令牌并返回给客户端，客户端需更新存储的令牌。 |
| **令牌过期拒绝请求** | 当 JWT 令牌过期时，服务器拒绝请求并返回 `401 Unauthorized` 错误。 |

---

## 依赖库安装

```bash
go get -u github.com/gin-gonic/gin
go get -u github.com/golang-jwt/jwt/v5
```

---

## 服务端代码（Server）

```go
package main

import (
	"fmt"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/golang-jwt/jwt/v5"
)

// JWT 密钥（生产环境应使用强密钥）
var jwtKey = []byte("your-256-bit-secret")

// 用户模拟数据
var users = map[string]string{
	"admin": "password123",
}

// JWT Claims 结构体
type Claims struct {
	UserID string `json:"user_id"`
	jwt.RegisteredClaims
}

// 生成 JWT 令牌
func generateJWT(userID string) (string, error) {
	expirationTime := time.Now().Add(15 * time.Minute)
	claims := &Claims{
		UserID: userID,
		RegisteredClaims: jwt.RegisteredClaims{
			ExpiresAt: jwt.NewNumericDate(expirationTime),
		},
	}
	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	return token.SignedString(jwtKey)
}

// 登录接口
func loginHandler(c *gin.Context) {
	var request struct {
		Username string `json:"username"`
		Password string `json:"password"`
	}
	if err := c.ShouldBindJSON(&request); err != nil {
		c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Invalid request"})
		return
	}

	password, exists := users[request.Username]
	if !exists || password != request.Password {
		c.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "Invalid credentials"})
		return
	}

	// 生成 JWT
	token, err := generateJWT(request.Username)
	if err != nil {
		c.AbortWithStatusJSON(http.StatusInternalServerError, gin.H{"error": "Failed to generate token"})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"token": token,
	})
}

// JWT 中间件（验证并刷新令牌）
func authMiddleware() gin.HandlerFunc {
	return func(c *gin.Context) {
		// 从请求头中获取 Token
		tokenString := c.GetHeader("Authorization")
		if tokenString == "" {
			c.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "Missing token"})
			return
		}

		// 解析 Token
		claims := &Claims{}
		token, err := jwt.ParseWithClaims(tokenString, claims, func(t *jwt.Token) (interface{}, error) {
			return jwtKey, nil
		})

		if err != nil || !token.Valid {
			c.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "Invalid or expired token"})
			return
		}

		// 验证通过后，生成新的 Token（刷新）
		newToken, err := generateJWT(claims.UserID)
		if err != nil {
			c.AbortWithStatusJSON(http.StatusInternalServerError, gin.H{"error": "Failed to refresh token"})
			return
		}

		// 将新 Token 返回给客户端
		c.Header("New-Token", newToken)

		// 将用户 ID 存入上下文，供后续处理使用
		c.Set("userID", claims.UserID)
		c.Next()
	}
}

// 受保护的接口
func protectedHandler(c *gin.Context) {
	userID, _ := c.Get("userID")
	c.JSON(http.StatusOK, gin.H{
		"message": "Protected resource",
		"user":    userID,
	})
}

func main() {
	r := gin.Default()

	// 公共路由
	r.POST("/login", loginHandler)

	// 受保护的路由
	protected := r.Group("/")
	protected.Use(authMiddleware())
	{
		protected.GET("/protected", protectedHandler)
	}

	fmt.Println("Server started at :8080")
	r.Run(":8080")
}
```

---

## 客户端代码（Client 示例）

```go
package main

import (
	"fmt"
	"io/ioutil"
	"net/http"
	"strings"
)

func main() {
	// 1. 登录获取初始 Token
	loginURL := "http://localhost:8080/login"
	loginData := `{"username":"admin","password":"password123"}`
	req, _ := http.NewRequest("POST", loginURL, strings.NewReader(loginData))
	req.Header.Set("Content-Type", "application/json")
	resp, _ := http.DefaultClient.Do(req)
	defer resp.Body.Close()
	body, _ := ioutil.ReadAll(resp.Body)

	var result map[string]string
	fmt.Println("Login Response:", string(body))

	// 提取 Token
	token := string(body)
	if strings.Contains(token, `"token":"`) {
		token = strings.Split(token, `"token":"`)[1]
		token = strings.Split(token, `"}`)[0]
	} else {
		fmt.Println("Login failed")
		return
	}

	// 2. 使用 Token 请求受保护资源（多次请求，自动刷新 Token）
	for i := 0; i < 5; i++ {
		resourceURL := "http://localhost:8080/protected"
		req, _ := http.NewRequest("GET", resourceURL, nil)
		req.Header.Set("Authorization", token)
		resp, _ := http.DefaultClient.Do(req)
		defer resp.Body.Close()
		body, _ := ioutil.ReadAll(resp.Body)

		fmt.Printf("Request %d Response: %s\n", i+1, body)

		// 提取新的 Token（刷新）
		newToken := resp.Header.Get("New-Token")
		if newToken != "" {
			token = newToken
		}
	}
}
```

---

## 5. 注意事项

### 优点
- **每次请求刷新 Token**：提升用户体验，避免频繁登录。
- **自动处理过期 Token**：服务器拒绝过期 Token 请求，并返回明确错误信息。
- **简单安全**：使用 HTTPS 传输 Token，防止窃听。

### 潜在问题
- **Token 未加密存储**：客户端需安全存储 Token（如加密 Cookie 或 LocalStorage）。
- **Token 刷新攻击**：恶意客户端可能利用刷新机制维持登录状态。建议结合黑名单（如 Redis）管理短期 Token。
- **Token 有效期控制**：刷新 Token 的有效期应较短（如 15 分钟），避免长期暴露。

---

## 🧪 6. 测试流程

1. **启动服务端**：运行 `server.go`。
2. **运行客户端**：运行 `client.go`。
3. **观察输出**：
   - 第一次请求成功，返回 Token。
   - 后续请求自动刷新 Token。
   - 如果等待超过 Token 有效期（15 分钟），后续请求会被拒绝。
