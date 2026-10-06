import json, pathlib, select, socket, subprocess, tempfile, time
root=pathlib.Path('/Users/aneeshkrishna/Documents/Codex/2026-10-05/v/work/lockstep')
build=root/'build-gcc-repair'
results=[]
with tempfile.TemporaryDirectory(prefix='lockstep-live-') as state:
    feeds=[]
    for _ in range(2):
        sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); sock.bind(('127.0.0.1',0)); feeds.append(sock)
    try:
        for phase in range(2):
            cmd=[str(build/'lockstep_exchange'),'--state-dir',state,'--tcp-port','0','--udp-a',str(feeds[0].getsockname()[1]),'--udp-b',str(feeds[1].getsockname()[1])]
            server=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            try:
                assert select.select([server.stdout],[],[],10)[0], 'Startup timeout'
                startup=server.stdout.readline().strip()
                assert startup.startswith('Exchange port '),startup
                port=startup.split()[2].rstrip(';'); next_sequence=int(startup.split()[-1])
                assert next_sequence==phase*2+1,startup
                assert not select.select(feeds,[],[],.2)[0], 'Recovery published duplicate live effects'
                client=subprocess.run([str(build/'lockstep_client'),port],input='new\ncancel\nquit\n',capture_output=True,text=True,timeout=10)
                assert client.returncode==0,client.stderr
                assert f'Accepted command {phase*2+1}' in client.stdout and f'Accepted command {phase*2+2}' in client.stdout,client.stdout
                packets=[[],[]]; deadline=time.monotonic()+3
                while time.monotonic()<deadline:
                    ready=select.select(feeds,[],[],.1)[0]
                    for feed in ready: packets[feeds.index(feed)].append(feed.recv(2048).hex())
                    if all(packets): break
                assert all(packets), 'No observable dual-feed publication'
                results.append({'phase':phase,'startup':startup,'client_exit':client.returncode,'stdout':client.stdout,'packets_a':packets[0],'packets_b':packets[1]})
            finally:
                server.terminate()
                remaining=server.communicate(timeout=5)[0]
                assert server.returncode==0,(server.returncode,remaining)
                for feed in feeds:
                    while select.select([feed],[],[],0)[0]: feed.recv(2048)
    finally:
        for feed in feeds: feed.close()
(root/'artifacts/live-client-smoke.json').write_text(json.dumps({'passed':True,'phases':results},indent=2)+'\n')
print('Real exchange/client: new/cancel, durable acknowledgements, restart continuation, both UDP feeds passed')
