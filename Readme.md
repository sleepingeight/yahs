```
surya@mamidi-s~$ oha -n 900000 -c 512 http://127.0.0.1:8000/index.html
Summary:
  Success rate:	100.00%
  Total:	21.9507 secs
  Slowest:	0.1382 secs
  Fastest:	0.0001 secs
  Average:	0.0125 secs
  Requests/sec:	41000.9385

  Total data:	508.12 MiB
  Size/request:	592 B
  Size/sec:	23.15 MiB

Response time histogram:
  0.000 [1]      |
  0.014 [640668] |■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
  0.028 [239300] |■■■■■■■■■■■
  0.042 [5672]   |
  0.055 [13795]  |
  0.069 [486]    |
  0.083 [0]      |
  0.097 [0]      |
  0.111 [0]      |
  0.124 [3]      |
  0.138 [75]     |

Response time distribution:
  10.00% in 0.0069 secs
  25.00% in 0.0092 secs
  50.00% in 0.0117 secs
  75.00% in 0.0144 secs
  90.00% in 0.0173 secs
  95.00% in 0.0198 secs
  99.00% in 0.0461 secs
  99.90% in 0.0541 secs
  99.99% in 0.0629 secs


Details (average, fastest, slowest):
  DNS+dialup:	0.0062 secs, 0.0000 secs, 0.1243 secs
  DNS-lookup:	0.0000 secs, 0.0000 secs, 0.0017 secs

Status code distribution:
  [200] 900000 responses


```