/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include "sdb.h"

#define NR_WP 32
#define WP_EXPR_LEN 256

typedef struct watchpoint {
  int NO;
  struct watchpoint *next;

  /* TODO: Add more members if necessary */
  char expr[WP_EXPR_LEN];
  word_t old_val;

} WP;

static WP wp_pool[NR_WP] = {};
static WP *head = NULL, *free_ = NULL;  // current watchpoint list and free watchpoint list

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
    wp_pool[i].expr[0] = '\0';
    wp_pool[i].old_val = 0;
  }

  head = NULL;
  free_ = wp_pool;
}

/* TODO: Implement the functionality of watchpoint */
void display_watchpoints() {
  if (head == NULL) {
    printf("No watchpoints.\n");
    return;
  }

  printf("Num     What                                Value\n");
  WP *p = head;
  while (p != NULL) {
    printf("%-7d %-35s " FMT_WORD "\n", p->NO, p->expr, p->old_val);
    p = p->next;
  }
}

void new_wp(char *expr_str) {
  if (free_ == NULL) {
    printf("No free watchpoint available.\n");
    return;
  }

  bool success = false;
  word_t val = expr(expr_str, &success);

  if (!success) {
    printf("Bad expression: %s\n", expr_str);
    return;
  }

  WP *wp = free_;
  free_ = free_->next;

  strncpy(wp->expr, expr_str, WP_EXPR_LEN - 1);
  wp->expr[WP_EXPR_LEN - 1] = '\0';

  wp->old_val = val;

  wp->next = head;
  head = wp;

  printf("Watchpoint %d: %s = " FMT_WORD "\n", wp->NO, wp->expr, wp->old_val);
}
  
void free_wp(int no) {
  WP *prev = NULL;
  WP *cur = head;

  while (cur != NULL) {
    if (cur->NO == no) {
      if (prev == NULL) {
        head = cur->next;
    } else {
      prev->next = cur->next;
    }

    cur->next = free_;
    free_ = cur;

    printf("Deleted watchpoint %d\n", no);
    return;
  }

  prev = cur;
  cur = cur->next;
}
printf("No watchpoint number %d.\n", no);
}

bool check_watchpoints() {
  bool stop = false;

  WP *p = head;
  while (p != NULL) {
    bool success = false;
    word_t new_val = expr(p->expr, &success);

    if (!success) {
      printf("Failed to evaluate watchpoint %d: %s\n", p->NO, p->expr);
      p = p->next;
      continue;
    }

    if (new_val != p->old_val) {
      printf("Watchpoint %d triggered: %s\n", p->NO, p->expr);
      printf("Old value = " FMT_WORD "\n", p->old_val);
      printf("New value = " FMT_WORD "\n", new_val);

      p->old_val = new_val;
      stop = true;
    }

    p = p->next;
  }

  return stop;
}


