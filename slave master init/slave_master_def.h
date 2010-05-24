#define VARPATH "/var/run/holey/"

struct master_sts{
  int ncopiers;
  char stderr[40];
  char log[40];
};

struct slave_sts{
  
  char cowdir[40];
  char masteradd[30];
  char stderr[40];
  char log[40];
};


