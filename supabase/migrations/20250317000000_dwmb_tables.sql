-- DWMB (dungeon): store instances, runs, and metrics for the benchmark.
create table if not exists dwmb_instances (
  id uuid primary key default gen_random_uuid(),
  instance_id text unique not null,
  tier int not null,
  split text not null check (split in ('train', 'test', 'counterfactual', 'unit_test')),
  payload jsonb not null,
  created_at timestamptz default now()
);

create index if not exists idx_dwmb_instances_split_tier on dwmb_instances(split, tier);

create table if not exists dwmb_runs (
  id uuid primary key default gen_random_uuid(),
  instance_id uuid not null references dwmb_instances(id) on delete cascade,
  agent_type text not null,
  seed int not null,
  trajectory jsonb,
  belief_log jsonb,
  goal_reached boolean,
  died boolean,
  hazard_activations jsonb,
  steps int,
  created_at timestamptz default now()
);

create index if not exists idx_dwmb_runs_instance_agent on dwmb_runs(instance_id, agent_type);

create table if not exists dwmb_metrics (
  id uuid primary key default gen_random_uuid(),
  run_id uuid not null references dwmb_runs(id) on delete cascade,
  pir_delta float,
  aupr float,
  goal_success boolean,
  survival boolean,
  hazard_count int,
  created_at timestamptz default now()
);

create index if not exists idx_dwmb_metrics_run on dwmb_metrics(run_id);

comment on table dwmb_instances is 'DWMB POMDP instances (JSON payload per report schema)';
comment on table dwmb_runs is 'Single agent run on an instance; trajectory and belief log for PIR';
comment on table dwmb_metrics is 'Computed metrics (PIR, AUPIR, success, etc.) per run';
