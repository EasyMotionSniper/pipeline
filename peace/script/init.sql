-- 假设用户表名为 `users`，根据实际表结构调整字段名
-- 先判断表是否存在（如果需要，可添加建表语句）
CREATE TABLE IF NOT EXISTS users (
  id INT AUTO_INCREMENT PRIMARY KEY,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  deleted_at DATETIME DEFAULT NULL,
  username VARCHAR(50) NOT NULL UNIQUE,
  password VARCHAR(255) NOT NULL,  -- 存储 bcrypt 加密后的密码（长度需足够）
  role ENUM('executor', 'viewer') DEFAULT 'viewer'
);

-- 插入用户1（仅当不存在时）
INSERT INTO users (username, password, role)
SELECT 
  'jak', 
  '123',
  'executor'
WHERE NOT EXISTS (
  SELECT 1 FROM users WHERE username = 'jak'
);

-- 插入用户2（仅当不存在时）
INSERT INTO users (username, password, role)
SELECT 
  'yww', 
  '123',
  'viewer'
WHERE NOT EXISTS (
  SELECT 1 FROM users WHERE username = 'yww'
);