package main

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"log"
	"time"

	_ "github.com/go-sql-driver/mysql"
	"github.com/google/uuid"
)

// 初始化数据库连接
func initDB() {
	var err error
	// 注意：实际生产环境中应使用环境变量或配置文件存储数据库信息
	dsn := "root:password@tcp(localhost:3306)/task_scheduler?parseTime=true"
	db, err = sql.Open("mysql", dsn)
	if err != nil {
		log.Fatalf("Failed to connect to database: %v", err)
	}

	// 测试连接
	err = db.Ping()
	if err != nil {
		log.Fatalf("Failed to ping database: %v", err)
	}

	// 创建表
	createTables()

	log.Println("Database connected successfully")
}

// 创建数据库表
func createTables() {
	// 流水线表
	pipelineTableSQL := `
	CREATE TABLE IF NOT EXISTS pipelines (
		id VARCHAR(36) PRIMARY KEY,
		name VARCHAR(255) NOT NULL,
		triggers JSON NOT NULL,
		status VARCHAR(20) NOT NULL DEFAULT 'created',
		created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
		updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE ON UPDATE CURRENT_TIMESTAMP
	);`

	_, err := db.Exec(pipelineTableSQL)
	if err != nil {
		log.Fatalf("Failed to create pipelines table: %v", err)
	}

	// 任务表
	taskTableSQL := `
	CREATE TABLE IF NOT EXISTS tasks (
		id VARCHAR(36) PRIMARY KEY,
		pipeline_id VARCHAR(36) NOT NULL,
		name VARCHAR(255) NOT NULL,
		image VARCHAR(255) NOT NULL,
		command TEXT NOT NULL,
		dependencies JSON NOT NULL,
		status VARCHAR(20) NOT NULL DEFAULT 'pending',
		stdout TEXT,
		stderr TEXT,
		created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
		updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
	 ON UPDATE CURRENT_TIMESTAMP,
		completed_at DATETIME,
		FOREIGN KEY (pipeline_id) REFERENCES pipelines(id)
	);`

	_, err = db.Exec(taskTableSQL)
	if err != nil {
		log.Fatalf("Failed to create tasks table: %v", err)
	}
}

// 保存流水线到数据库
func savePipelineToDB(pipeline *Pipeline) (string, error) {
	// 生成UUID作为流水线ID
	pipelineID := uuid.New().String()
	if pipeline.ID != "" {
		pipelineID = pipeline.ID
	}

	// 序列化triggers为JSON
	triggersJSON, err := json.Marshal(pipeline.Triggers)
	if err != nil {
		return "", err
	}

	// 插入流水线记录
	_, err = db.Exec(`
		INSERT INTO pipelines (id, name, triggers, status)
		VALUES (?, ?, ?, ?)`,
		pipelineID, pipeline.Name, string(triggersJSON), PipelineStatusCreated,
	)
	if err != nil {
		return "", err
	}

	// 插入任务记录
	for i := range pipeline.Tasks {
		task := &pipeline.Tasks[i]
		task.PipelineID = pipelineID
		
		// 生成任务ID
		taskID := uuid.New().String()
		if task.ID != "" {
			taskID = task.ID
		}
		
		// 序列化dependencies为JSON
		depsJSON, err := json.Marshal(task.Dependencies)
		if err != nil {
			return "", err
		}

		_, err = db.Exec(`
			INSERT INTO tasks (id, pipeline_id, name, image, command, dependencies, status)
			VALUES (?, ?, ?, ?, ?, ?, ?)`,
			taskID, pipelineID, task.Name, task.Image, task.Command, string(depsJSON), TaskStatusPending,
		)
		if err != nil {
			return "", err
		}
	}

	return pipelineID, nil
}

// 从数据库获取所有流水线
func getPipelinesFromDB() ([]map[string]interface{}, error) {
	rows, err := db.Query(`
		SELECT id, name, triggers, status, created_at, updated_at
		FROM pipelines
		ORDER BY created_at DESC
	`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var pipelines []map[string]interface{}
	for rows.Next() {
		var id, name, status string
		var triggersJSON string
		var createdAt, updatedAt time.Time

		err := rows.Scan(&id, &name, &triggersJSON, &status, &createdAt, &updatedAt)
		if err != nil {
			return nil, err
		}

		var triggers []Trigger
		json.Unmarshal([]byte(triggersJSON), &triggers)

		pipelines = append(pipelines, map[string]interface{}{
			"id":         id,
			"name":       name,
			"triggers":   triggers,
			"status":     status,
			"created_at": createdAt,
			"updated_at": updatedAt,
		})
	}

	return pipelines, nil
}

// 从数据库获取特定流水线
func getPipelineFromDB(pipelineID string) (*Pipeline, error) {
	row := db.QueryRow(`
		SELECT id, name, triggers
		FROM pipelines
		WHERE id = ?
	`, pipelineID)

	var id, name string
	var triggersJSON string
	err := row.Scan(&id, &name, &triggersJSON)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}

	var triggers []Trigger
	json.Unmarshal([]byte(triggersJSON), &triggers)

	// 获取流水线的任务
	rows, err := db.Query(`
		SELECT id, name, image, command, dependencies
		FROM tasks
		WHERE pipeline_id = ?
	`, pipelineID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var tasks []Task
	for rows.Next() {
		var taskID, taskName, image, command, depsJSON string
		err := rows.Scan(&taskID, &taskName, &image, &command, &depsJSON)
		if err != nil {
			return nil, err
		}

		var dependencies []string
		json.Unmarshal([]byte(depsJSON), &dependencies)

		tasks = append(tasks, Task{
			ID:           taskID,
			Name:         taskName,
			Image:        image,
			Command:      command,
			Dependencies: dependencies,
			PipelineID:   pipelineID,
		})
	}

	return &Pipeline{
		ID:       id,
		Name:     name,
		Triggers: triggers,
		Tasks:    tasks,
	}, nil
}

// 更新任务状态
func updateTaskStatusInDB(update TaskStatusUpdate) error {
	_, err := db.Exec(`
		UPDATE tasks
		SET status = ?, stdout = ?, stderr = ?, completed_at = ?
		WHERE id = ?
	`, update.Status, update.Stdout, update.Stderr, update.CompletedAt, update.TaskID)
	return err
}

// 获取任务状态
func getTaskStatusFromDB(taskID string) (map[string]interface{}, error) {
	row := db.QueryRow(`
		SELECT id, pipeline_id, name, status, stdout, stderr, created_at, completed_at
		FROM tasks
		WHERE id = ?
	`, taskID)

	var id, pipelineID, name, status, stdout, stderr string
	var createdAt time.Time
	var completedAt sql.NullTime

	err := row.Scan(&id, &pipelineID, &name, &status, &stdout, &stderr, &createdAt, &completedAt)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}

	result := map[string]interface{}{
		"id":           id,
		"pipeline_id":  pipelineID,
		"name":         name,
		"status":       status,
		"stdout":       stdout,
		"stderr":       stderr,
		"created_at":   createdAt,
	}

	if completedAt.Valid {
		result["completed_at"] = completedAt.Time
	}

	return result, nil
}

// 获取依赖于指定任务的任务
func getDependentTasks(taskID string) ([]Task, error) {
	// 首先获取任务所属的流水线
	row := db.QueryRow(`
		SELECT pipeline_id
		FROM tasks
		WHERE id = ?
	`, taskID)

	var pipelineID string
	err := row.Scan(&pipelineID)
	if err != nil {
		return nil, err
	}

	// 查询所有依赖于该任务的任务
	rows, err := db.Query(`
		SELECT id, name, image, command, dependencies
		FROM tasks
		WHERE pipeline_id = ?
	`, pipelineID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var dependentTasks []Task
	for rows.Next() {
		var id, name, image, command, depsJSON string
		err := rows.Scan(&id, &name, &image, &command, &depsJSON)
		if err != nil {
			return nil, err
		}

		var dependencies []string
		json.Unmarshal([]byte(depsJSON), &dependencies)

		// 检查当前任务是否依赖于指定任务
		isDependent := false
		for _, dep := range dependencies {
			if dep == taskID {
				isDependent = true
				break
			}
		}

		if isDependent {
			dependentTasks = append(dependentTasks, Task{
				ID:           id,
				Name:         name,
				Image:        image,
				Command:      command,
				Dependencies: dependencies,
				PipelineID:   pipelineID,
			})
		}
	}

	return dependentTasks, nil
}

// 检查任务的所有依赖是否都已完成
func checkAllDependenciesCompleted(taskID string) (bool, error) {
	row := db.QueryRow(`
		SELECT dependencies
		FROM tasks
		WHERE id = ?
	`, taskID)

	var depsJSON string
	err := row.Scan(&depsJSON)
	if err != nil {
		return false, err
	}

	var dependencies []string
	json.Unmarshal([]byte(depsJSON), &dependencies)

	if len(dependencies) == 0 {
		return true, nil
	}

	// 检查所有依赖任务的状态
	placeholders := make([]string, len(dependencies))
	args := make([]interface{}, len(dependencies))
	for i, dep := range dependencies {
		placeholders[i] = "?"
		args[i] = dep
	}

	query := fmt.Sprintf(`
		SELECT COUNT(*)
		FROM tasks
		WHERE id IN (%s) AND status != ?
	`, strings.Join(placeholders, ","))

	args = append(args, TaskStatusSuccess)

	row = db.QueryRow(query, args...)
	var count int
	err = row.Scan(&count)
	if err != nil {
		return false, err
	}

	// 如果有任何依赖任务未成功完成，则返回false
	return count == 0, nil
}

// 触发流水线执行
func triggerPipelineByID(pipelineID string) error {
	// 更新流水线状态为运行中
	_, err := db.Exec(`
		UPDATE pipelines
		SET status = ?
		WHERE id = ?
	`, PipelineStatusRunning, pipelineID)
	if err != nil {
		return err
	}

	// 获取所有没有依赖的任务，作为起始任务
	rows, err := db.Query(`
		SELECT id, name, image, command, dependencies
		FROM tasks
		WHERE pipeline_id = ? AND JSON_LENGTH(dependencies) = 0
	`, pipelineID)
	if err != nil {
		return err
	}
	defer rows.Close()

	// 提交这些任务到执行器
	for rows.Next() {
		var id, name, image, command, depsJSON string
		err := rows.Scan(&id, &name, &image, &command, &depsJSON)
		if err != nil {
			return err
		}

		var dependencies []string
		json.Unmarshal([]byte(depsJSON), &dependencies)

		task := Task{
			ID:           id,
			Name:         name,
			Image:        image,
			Command:      command,
			Dependencies: dependencies,
			PipelineID:   pipelineID,
		}

		taskExecutor.SubmitTask(task)
	}

	return nil
}

// 检查流水线是否已完成
func checkPipelineCompletion(pipelineID string) error {
	// 检查是否有任务正在运行
	row := db.QueryRow(`
		SELECT COUNT(*)
		FROM tasks
		WHERE pipeline_id = ? AND (status = ? OR status = ?)
	`, pipelineID, TaskStatusPending, TaskStatusRunning)

	var runningCount int
	err := row.Scan(&runningCount)
	if err != nil {
		return err
	}

	// 如果还有任务在运行或待处理，不更新状态
	if runningCount > 0 {
		return nil
	}

	// 检查是否有任务失败
	row = db.QueryRow(`
		SELECT COUNT(*)
		FROM tasks
		WHERE pipeline_id = ? AND status = ?
	`, pipelineID, TaskStatusFailed)

	var failedCount int
	err = row.Scan(&failedCount)
	if err != nil {
		return err
	}

	// 更新流水线状态
	var newStatus PipelineStatus
	if failedCount > 0 {
		newStatus = PipelineStatusFailed
	} else {
		newStatus = PipelineStatusCompleted
	}

	_, err = db.Exec(`
		UPDATE pipelines
		SET status = ?
		WHERE id = ?
	`, newStatus, pipelineID)

	return err
}
    