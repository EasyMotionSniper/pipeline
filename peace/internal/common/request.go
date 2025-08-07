package common

type LoginRequest struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

type Response struct {
	Code  int    `json:"code"`
	Error string `json:"error"`
}

type LoginResponse struct {
	Response
	Token string `json:"token"`
}
