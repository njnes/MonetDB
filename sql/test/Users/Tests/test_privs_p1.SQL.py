###
# SELECT, INSERT, UPDATE, DELETE a table for which the USER has GRANTs (possible).
###

import os, sys
try:
    from MonetDBtesting import process
except ImportError:
    import process

with process.client('sql', user='my_user', passwd='p1',
                    stdin=open(os.path.join(os.getenv('RELSRCDIR'), os.pardir, 'test_privs.sql')),
                    stdout=process.PIPE, stderr=process.PIPE) as clt:
    out, err = clt.communicate()
    sys.stdout.write(out)
    sys.stderr.write(err)
