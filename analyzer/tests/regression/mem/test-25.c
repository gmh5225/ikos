// SAFE
extern void __ikos_assert(int);
extern int __ikos_unknown();

struct foo {
  int p;
  int q;
};

struct foo x;
int main() {
  x.p = 9;
  x.q = 20;

  int p1, p2;

  if (__ikos_unknown())
    p1 = x.p;
  else
    p1 = x.q;

  p2 = x.q;

  __ikos_assert(p1 >= 9 && p1 <= 20);
  __ikos_assert(p2 == 20);
  return p1 + p2;
}
