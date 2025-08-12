package scheduler

import (
	"context"
	"encoding/json"
	"log"
	"pace/internal/server/dao"
	"pace/internal/server/model"
	"pace/pkg/queue"
	"sync"

	"github.com/hibiken/asynq"
)

var schedulerService *SchedulerService
var once sync.Once

func StartScheduler() error {
	return GetSchedulerService().Start()
}

func GetSchedulerService() *SchedulerService {
	once.Do(func() {
		scheduler := asynq.NewScheduler(asynq.RedisClientOpt{Addr: "localhost:6379", Password: "justredis"}, nil)
		schedulerService = newSchedulerService(scheduler)
		// if err := schedulerService.LoadAllSchedules(); err != nil {
		// 	log.Fatalf("Failed to load schedules: %v", err)
		// }
	})
	return schedulerService
}

type SchedulerService struct {
	scheduler     *asynq.Scheduler
	mu            sync.Mutex
	scheduledJobs map[uint]string // pipeline ID -> 调度任务ID
}

func newSchedulerService(scheduler *asynq.Scheduler) *SchedulerService {
	return &SchedulerService{
		scheduler:     scheduler,
		scheduledJobs: make(map[uint]string),
	}
}

func (s *SchedulerService) Start() error {
	log.Println("Starting scheduler...")
	return s.scheduler.Start()
}

func (s *SchedulerService) UpsertPipelineSchedule(pipelineVersion *model.PipelineVersion) error {

	s.mu.Lock()
	defer s.mu.Unlock()
	pipelineID := pipelineVersion.PipelineID

	if jobID, exists := s.scheduledJobs[pipelineID]; exists {
		if err := s.scheduler.Unregister(jobID); err != nil {
			log.Printf("Failed to remove existing job for pipeline %d: %v", pipelineID, err)
		} else {
			log.Printf("Removed existing job %s for pipeline %d", jobID, pipelineID)
		}
		delete(s.scheduledJobs, pipelineID)
	}

	pipelineConfig, err := queue.ParsePipelineConfig(pipelineVersion.Config)
	if err != nil {
		return err
	}

	var jobID string
	for _, trigger := range pipelineConfig.Triggers {
		if trigger.Cron == "" {
			continue
		}
		req := queue.PipelineExecuteInfo{
			PipelineVersionID: pipelineVersion.ID,
			Tasks:             pipelineConfig.Tasks,
		}
		// turn req into bytes
		jsonData, err := json.Marshal(req)
		if err != nil {
			return err
		}
		task := asynq.NewTask(queue.PIPELINE_EXECUTE, jsonData)
		jobID, err = s.scheduler.Register(trigger.Cron, task)
		if err != nil {
			log.Printf("Error scheduling pipeline %d: %v", pipelineVersion.PipelineID, err)
			return err
		}
		break
	}
	if jobID == "" {
		log.Printf("No valid cron trigger found for pipeline %d", pipelineVersion.PipelineID)
		return nil
	}
	s.scheduledJobs[pipelineID] = jobID
	log.Printf("Scheduled job %s for pipeline %d", jobID, pipelineID)

	return nil
}

func (s *SchedulerService) LoadAllSchedules() error {

	pipelineDao := dao.NewPipelineDao()
	// 获取所有管道
	pipelineVersions, err := pipelineDao.GetAllPipelineVersions(context.Background())
	if err != nil {
		return err
	}

	for _, pipelineVersion := range pipelineVersions {
		if err := s.UpsertPipelineSchedule(pipelineVersion); err != nil {
			log.Printf("Error scheduling pipeline %d: %v", pipelineVersion.PipelineID, err)
		}

	}

	return nil
}
