extern void __ikos_assert(int);
extern int __ikos_unknown();

void foo() {
  int i, x;

  x = 10;
  for (i = 0; i <= 10; i++) {
    x++;
  }
  __ikos_assert(x <= 21);
}
