###
# Use functions that contain SELECT. INSERT, UPDATE, DELETE on a
# table for which the USER does *not* have GRANTs (not possible).
###


import os, sys
try:
    from MonetDBtesting import process
except ImportError:
    import process

with process.client('sql', user='my_user2', passwd='p2',
                    stdin=open(os.path.join(os.getenv('RELSRCDIR'), os.pardir, 'test_privs2.sql')),
                    stdout=process.PIPE, stderr=process.PIPE) as clt:
    out, err = clt.communicate()
    sys.stdout.write(out)
    sys.stderr.write(err)
