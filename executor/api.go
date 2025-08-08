package executor

import (
	"net/http"
	"os"
	"strconv"

	"yourproject/db"
	"yourproject/executor"
	"yourproject/parser"
	"yourproject/scheduler"

	"github.com/gin-gonic/gin"
)

var (
	exec  *executor.Executor
	sched *scheduler.Scheduler
)

func main() {
	// 初始化数据库
	if err := db.Init("pipeline.db"); err != nil {
		panic(err)
	}

	// 初始化执行器
	var err error
	exec, err = executor.NewExecutor(10) // 最大并发10
	if err != nil {
		panic(err)
	}

	// 初始化调度器
	sched = scheduler.NewScheduler(exec)
	sched.Start()
	defer sched.Stop()

	// 设置路由
	r := gin.Default()

	// API路由
	api := r.Group("/api")
	{
		api.POST("/pipelines", createOrUpdatePipeline)
		api.GET("/pipelines/:name", getPipeline)
		api.POST("/pipelines/:name/trigger", triggerPipeline)
		api.GET("/pipelines/:name/executions", listPipelineExecutions)
		api.GET("/executions/:id/tasks", listTaskExecutions)
	}

	// Webhook路由
	r.POST("/webhooks/:name", handleWebhook)

	// 启动服务器
	r.Run(":8080")
}

// 创建或更新流水线
func createOrUpdatePipeline(c *gin.Context) {
	// 从请求中获取YAML文件
	file, _, err := c.Request.FormFile("file")
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "无法获取文件: " + err.Error()})
		return
	}
	defer file.Close()

	// 读取文件内容
	data, err := os.ReadFile(file.Name())
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "无法读取文件: " + err.Error()})
		return
	}

	// 解析YAML
	pipeline, err := parser.ParseYAML(data)
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "解析YAML失败: " + err.Error()})
		return
	}

	// 保存到数据库
	if err := db.CreateOrUpdatePipeline(pipeline); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "保存流水线失败: " + err.Error()})
		return
	}

	// 更新调度
	if err := sched.SchedulePipeline(pipeline); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "设置调度失败: " + err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"message": "流水线创建/更新成功",
		"name":    pipeline.Name,
		"version": pipeline.Version,
	})
}

// 获取流水线信息
func getPipeline(c *gin.Context) {
	name := c.Param("name")
	version := c.Query("version")

	var pipeline *models.Pipeline
	var err error

	if version != "" {
		// 按版本获取
		ver, err := strconv.Atoi(version)
		if err != nil {
			c.JSON(http.StatusBadRequest, gin.H{"error": "无效的版本号"})
			return
		}
		pipeline, err = db.GetPipelineByVersion(name, ver)
	} else {
		// 获取最新版本
		pipeline, err = db.GetLatestPipeline(name)
	}

	if err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "未找到流水线: " + err.Error()})
		return
	}

	c.JSON(http.StatusOK, pipeline)
}

// 手动触发流水线
func triggerPipeline(c *gin.Context) {
	name := c.Param("name")

	if err := sched.TriggerPipeline(name, "manual"); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "触发流水线失败: " + err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{"message": "流水线已触发"})
}

// 处理Webhook触发
func handleWebhook(c *gin.Context) {
	name := c.Param("name")

	if err := sched.TriggerPipeline(name, "webhook"); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "触发流水线失败: " + err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{"message": "Webhook已接收，流水线已触发"})
}

// 列出流水线的执行记录
func listPipelineExecutions(c *gin.Context) {
	name := c.Param("name")

	// 获取流水线
	pipeline, err := db.GetLatestPipeline(name)
	if err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "未找到流水线: " + err.Error()})
		return
	}

	// 查询执行记录
	var executions []models.PipelineExecution
	result := db.DB.Where("pipeline_id = ?", pipeline.ID).Order("start_time DESC").Find(&executions)
	if result.Error != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "查询执行记录失败: " + result.Error.Error()})
		return
	}

	c.JSON(http.StatusOK, executions)
}

// 列出执行记录中的任务执行情况
func listTaskExecutions(c *gin.Context) {
	execID := c.Param("id")

	// 转换为uint
	id, err := strconv.ParseUint(execID, 10, 32)
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "无效的执行ID"})
		return
	}

	// 查询任务执行记录
	var taskExecutions []models.TaskExecution
	result := db.DB.Where("pipeline_exec_id = ?", id).Find(&taskExecutions)
	if result.Error != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "查询任务执行记录失败: " + result.Error.Error()})
		return
	}

	c.JSON(http.StatusOK, taskExecutions)
}
