package parhash

import (
	"context"
	"log"
	"net"
	"sync"

	"github.com/pkg/errors"
	"golang.org/x/sync/semaphore"
	"google.golang.org/grpc"

	hashpb "fs101ex/pkg/gen/hashsvc"
	parhashpb "fs101ex/pkg/gen/parhashsvc"
	"fs101ex/pkg/workgroup"
)

type Config struct {
	ListenAddr   string
	BackendAddrs []string
	Concurrency  int
}

// Implement a server that responds to ParallelHash()
// as declared in /proto/parhash.proto.
//
// The implementation of ParallelHash() must not hash the content
// of buffers on its own. Instead, it must send buffers to backends
// to compute hashes. Buffers must be fanned out to backends in the
// round-robin fashion.
//
// For example, suppose that 2 backends are configured and ParallelHash()
// is called to compute hashes of 5 buffers. In this case it may assign
// buffers to backends in this way:
//
//	backend 0: buffers 0, 2, and 4,
//	backend 1: buffers 1 and 3.
//
// Requests to hash individual buffers must be issued concurrently.
// Goroutines that issue them must run within /pkg/workgroup/Wg. The
// concurrency within workgroups must be limited by Server.sem.
//
// WARNING: requests to ParallelHash() may be concurrent, too.
// Make sure that the round-robin fanout works in that case too,
// and evenly distributes the load across backends.
type Server struct {
	conf Config

	sem     *semaphore.Weighted
	lock    sync.Mutex
	current int

	stop context.CancelFunc
	l    net.Listener
	wg   sync.WaitGroup
}

func New(conf Config) *Server {
	return &Server{
		conf:    conf,
		sem:     semaphore.NewWeighted(int64(conf.Concurrency)),
		current: 0,
	}
}

func (s *Server) Start(ctx context.Context) (err error) {
	defer func() { err = errors.Wrap(err, "Start()") }()

	ctx, s.stop = context.WithCancel(ctx)
	s.l, err = net.Listen("tcp", s.conf.ListenAddr)
	if err != nil {
		return err
	}
	srv := grpc.NewServer()
	parhashpb.RegisterParallelHashSvcServer(srv, s)

	s.wg.Add(2)
	go func() {
		defer s.wg.Done()
		srv.Serve(s.l)
	}()
	go func() {
		defer s.wg.Done()
		<-ctx.Done()
		s.l.Close()
	}()
	return nil
}

func (s *Server) ListenAddr() string {
	return s.l.Addr().String()
}

func (s *Server) Stop() {
	s.stop()
	s.wg.Wait()
}

func (s *Server) ParallelHash(ctx context.Context, req *parhashpb.ParHashReq) (resp *parhashpb.ParHashResp, err error) {
	connections := make([]*grpc.ClientConn, len(s.conf.BackendAddrs))
	clients := make([]hashpb.HashSvcClient, len(s.conf.BackendAddrs))
	for i := range connections {
		connections[i], err = grpc.Dial(s.conf.BackendAddrs[i], grpc.WithInsecure())
		if err != nil {
			log.Fatalf("failed to connect to %q: %v", s.conf.BackendAddrs[i], err)
		}
		defer connections[i].Close()
		clients[i] = hashpb.NewHashSvcClient(connections[i])
	}
	var (
		wg     = workgroup.New(workgroup.Config{Sem: s.sem})
		hashes = make([][]byte, len(req.Data))
	)
	for i := range req.Data {
		query_nr := i
		wg.Go(ctx, func(ctx context.Context) error {
			s.lock.Lock()
			backend_nr := s.current
			s.current += 1
			if s.current >= len(s.conf.BackendAddrs) {
				s.current = 0
			}
			s.lock.Unlock()
			resp, err := clients[backend_nr].Hash(ctx, &hashpb.HashReq{Data: req.Data[query_nr]})
			if err != nil {
				return err
			}
			s.lock.Lock()
			hashes[query_nr] = resp.Hash
			s.lock.Unlock()
			return nil
		})
	}
	if err := wg.Wait(); err != nil {
		log.Fatalf("failed to hash data: %v", err)
	}
	return &parhashpb.ParHashResp{Hashes: hashes}, nil
}
