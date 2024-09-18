#include <bits/stdc++.h>
#include "foo/a.h"
#include "foo/b.h"
#include "tool.h"
#include "proto/ps/ps.pb.h"

int main(int argc, char* argv[]) {
    B b;
    b.test();

    A a;
    a.read(argv[0]);
    ExecuteCmd("ls -a");

    SyncRequest req;
    req.mutable_bi()->set_name("name1");
    req.mutable_bi()->set_info("info1");
    printf("the length of req serialization is %lu\n", req.SerializeAsString().size());
    return 0;
}
