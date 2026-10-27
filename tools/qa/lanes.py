"""Drive Thief2MP test lanes on the Windows desktop from WSL.

  python3 -I lanes.py setup <stock|fixed> host:idle client1:walker   copy DLL, ini, bots; clear telemetry
  python3 -I lanes.py launch host client1                            start the games, place windows
  python3 -I lanes.py qa <lane> "<cmd>; <cmd>; ..."                  queue QA commands (see mod/src/qa.c)
  python3 -I lanes.py shot <lane> [name]                             screen capture of that window
  python3 -I lanes.py wait_ingame <lane>... [timeout_s]              until every lane has a local player
  python3 -I lanes.py kill
"""
import os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEST = os.environ.get('T2_TEST_DIR', '/mnt/d/thief_coop_test')   # Windows folder with the lanes, as a WSL path
WIN_TEST = subprocess.run(['wslpath', '-w', TEST], capture_output=True, text=True).stdout.strip()
PLACE = {'host': (0, 0), 'client1': (1020, 0), 'client2': (0, 640), 'client3': (1020, 640)}
MODES = {'stock': (0, 0), 'fixed': (1, 1), 'send_only': (1, 0), 'recv_only': (0, 1)}


def ps(script, *args):
    return subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                           f'{WIN_TEST}\\{script}', *args], capture_output=True, text=True).stdout


def pid_of(lane):
    out = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
                          f"Get-Process Thief2MP -ErrorAction SilentlyContinue | Where-Object {{ $_.Path -like '*\\{lane}\\*' }} | "
                          "Select-Object -ExpandProperty Id"], capture_output=True, text=True).stdout.split()
    return int(out[0]) if out else None


def setup(mode, *lane_bots):
    """lane_bots: "lane:bot" or "lane:bot:mode" (per-lane mode override, e.g. a player without the send fix)."""
    for tool in ('place.ps1', 'shot.ps1', 'input.ps1', 'unfocus.ps1'):
        shutil.copy(f'{ROOT}/tools/win/{tool}', f'{TEST}/{tool}')
    for lb in lane_bots:
        lane, bot, *lm = lb.split(':')
        sf, rf = MODES[lm[0] if lm else mode]
        d = f'{TEST}/{lane}'
        shutil.copy(f'{ROOT}/mod/build/dinput.dll', f'{d}/dinput.dll')
        shutil.copy(f'{ROOT}/tools/qa/bots/{bot}.txt', f'{d}/bot_{lane}.txt')
        with open(f'{d}/T2MPFix.ini', 'w') as f:
            f.write(f'[T2MPFix]\nsend_fix={sf}\nrecv_fix={rf}\ntelemetry=1\nmax_players={os.environ.get("MAXP", "0")}\n[qa]\nenable=1\nbot=bot_{lane}.txt\n')
            net = os.environ.get('NETSIM')            # "delay_ms,jitter_ms,loss_pct[,seed]"
            if net:
                v = (net.split(',') + ['1'])[:4]
                f.write(f'net_delay_ms={v[0]}\nnet_jitter_ms={v[1]}\nnet_loss_pct={v[2]}\nnet_seed={v[3]}\n')
        if os.path.isdir(f'{d}/MOVIES'):            # test copies only: no cutscenes, so sessions start fast
            os.rename(f'{d}/MOVIES', f'{d}/MOVIES_off')
        for fn in ('T2MPFix_tel.csv', 'T2MPFix.log', 'qa_status.txt', 'qa_cmd.txt', 'crash.dmp'):
            if os.path.exists(f'{d}/{fn}'):
                os.remove(f'{d}/{fn}')
    print(f'setup {mode}: {lane_bots}')


def launch(*lanes):
    for lane in lanes:
        subprocess.Popen([f'{TEST}/{lane}/Thief2MP.exe'], cwd=f'{TEST}/{lane}',
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2)
    for lane in lanes:
        for _ in range(60):
            log = f'{TEST}/{lane}/T2MPFix.log'
            if os.path.exists(log) and 'subclassed' in open(log).read():
                break
            time.sleep(0.5)
        else:
            print(f'{lane}: QA channel not ready'); continue
        place(lane)
        qa(lane, 'restore; activate')
    print('launched', lanes)


def place(lane, shot=''):
    x, y = PLACE.get(lane, (0, 0))
    ps('place.ps1', '-ProcId', str(pid_of(lane)), '-X', str(x), '-Y', str(y), *(['-Shot', shot] if shot else []))


def qa(lane, cmds, wait=True):
    path = f'{TEST}/{lane}/qa_cmd.txt'
    status = f'{TEST}/{lane}/qa_status.txt'
    if os.path.exists(status):
        os.remove(status)
    with open(path + '.tmp', 'w') as f:
        f.write('\n'.join(c.strip() for c in cmds.split(';') if c.strip()) + '\n')
    os.replace(path + '.tmp', path)
    if wait:
        for _ in range(600):
            if os.path.exists(status):
                return open(status).read()
            time.sleep(0.1)
        print(f'{lane}: qa timeout')


def shot(lane, name=None):
    os.makedirs(f'{TEST}/shots', exist_ok=True)
    name = name or lane
    place(lane, f'{WIN_TEST}\\shots\\{name}.png')
    print(f'{TEST}/shots/{name}.png')


def wait_ingame(*args):
    lanes = [a for a in args if not a.isdigit()]
    timeout = int(next((a for a in args if a.isdigit()), '180'))
    t0 = time.time()
    while time.time() - t0 < timeout:
        ok = []
        for lane in lanes:
            st = qa(lane, 'status') or ''
            ok.append('local_player 0' not in st and 'local_player' in st)
        if all(ok):
            print('in game:', lanes); return True
        time.sleep(2)
    print('timeout waiting for', lanes); return False


def unfocus():
    """Injected input only works while the game window is NOT the foreground window."""
    ps('unfocus.ps1')


# Menu coordinates in the engine's mouse space (screenshot px of the 1008x786 window: ((x-4)/1.6, (y-33)/1.6)).
MENU = {'multiplayer': (312, 233), 'first_option': (310, 92), 'second_option': (310, 114), 'next': (457, 428),
        'start_game': (329, 286)}


def lobby(host='host', *clients):
    """Main menu -> host lobby; clients join. Leaves the host in the lobby."""
    clients = clients or ('client1',)
    unfocus()
    m = MENU
    qa(host, f"click {m['multiplayer'][0]} {m['multiplayer'][1]}; wait 1500; click {m['next'][0]} {m['next'][1]}; wait 1500;"
             f"click {m['next'][0]} {m['next'][1]}; wait 2000; status")
    for c in clients:
        qa(c, f"click {m['multiplayer'][0]} {m['multiplayer'][1]}; wait 1500; click {m['second_option'][0]} {m['second_option'][1]};"
              f"wait 500; click {m['next'][0]} {m['next'][1]}; wait 1500; click {m['next'][0]} {m['next'][1]}; wait 4000; status")
    print('lobby filled')


def start_session(host='host', *clients):
    """Main menu -> host lobby; clients join 127.0.0.1; host starts a new game (mission 1, Normal)."""
    clients = clients or ('client1',)
    unfocus()
    m = MENU
    qa(host, f"click {m['multiplayer'][0]} {m['multiplayer'][1]}; wait 1500; click {m['next'][0]} {m['next'][1]}; wait 1500;"
             f"click {m['next'][0]} {m['next'][1]}; wait 2000; status")
    for c in clients:
        qa(c, f"click {m['multiplayer'][0]} {m['multiplayer'][1]}; wait 1500; click {m['second_option'][0]} {m['second_option'][1]};"
              f"wait 500; click {m['next'][0]} {m['next'][1]}; wait 1500; click {m['next'][0]} {m['next'][1]}; wait 3000; status")
    time.sleep(2)
    qa(host, f"click {m['next'][0]} {m['next'][1]}; wait 2000; click {m['second_option'][0]} {m['second_option'][1]}; wait 2000;"
             f"click {m['start_game'][0]} {m['start_game'][1]}; wait 2000; status")
    print('session started')


def kill():
    subprocess.run(['taskkill.exe', '/IM', 'Thief2MP.exe', '/F'], capture_output=True)
    for _ in range(40):
        out = subprocess.run(['tasklist.exe', '/FI', 'IMAGENAME eq Thief2MP.exe'], capture_output=True, text=True).stdout
        if 'Thief2MP' not in out:
            break
        time.sleep(0.5)
    time.sleep(1)


if __name__ == '__main__':
    fn = globals()[sys.argv[1]]
    r = fn(*sys.argv[2:])
    if isinstance(r, str):
        print(r)
