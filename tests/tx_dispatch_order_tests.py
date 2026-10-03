"""Compile and exercise the actual network.c post-RX source slice in both modes.

Only the external calls are mocked. The production order and failure branches
are copied verbatim; this is not an independent model of the intended order.
Run in a VS x64 developer shell with --cc cl, or with clang on a host.
"""
from pathlib import Path
import argparse
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PREFIX = r'''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef int32_t NTSTATUS;
#define NT_SUCCESS(s) ((s)>=0)
typedef struct {unsigned NetworkPhase;NTSTATUS NetworkStatus;} ADAPTER;
typedef struct {unsigned Sends;} NETWORK;
typedef unsigned CYW_TX_POST_OBSERVATION;
static char trace[32];static unsigned cursor,Failures,PumpFrames;
static NTSTATUS PumpStatus;static uint64_t Clock;
static void logcall(char c){if(cursor<sizeof(trace)-1){trace[cursor++]=c;trace[cursor]=0;}}
static uint64_t KeQueryInterruptTime(void){return Clock;}
static void CywTxDiagPostBegin(ADAPTER *a,uint64_t t,CYW_TX_POST_OBSERVATION *o)
{(void)a;(void)t;*o=0;logcall('B');}
static NTSTATUS pump(ADAPTER *a,unsigned *q,unsigned *sent)
{(void)a;(void)q;*sent=PumpFrames;logcall('P');return PumpStatus;}
#define CywTxCreditPostReceivePump pump
#define CywTxPostReceivePump pump
static void CywTxDiagPostEnd(ADAPTER *a,unsigned s,CYW_TX_POST_OBSERVATION *o)
{(void)a;(void)s;(void)o;logcall('E');}
static void CywTransportSample(ADAPTER *a){(void)a;logcall('T');}
static void CywMeasuredDiagnostics(ADAPTER *a,unsigned s,NTSTATUS n)
{(void)a;(void)s;(void)n;logcall('D');Clock+=200000;}
static int run(unsigned due,unsigned phaseChanged)
{
    ADAPTER adapter={1,0},*A=&adapter;NETWORK network={0},*N=&network;
    unsigned sentAfter=0,lastPhase=phaseChanged?0:1;
    uint64_t nextSnapshot=due?0:1000000000,rxDiagEnd=Clock;
    CYW_TX_POST_OBSERVATION postObservation;NTSTATUS Status;
'''
SUFFIX = r'''
    (void)lastPhase;(void)nextSnapshot;logcall('I');return 0;
Failed:
    logcall('F');return 1;
}
int main(void)
{
    unsigned due,phase,fail,frames;
    for(due=0;due<2;++due)for(phase=0;phase<2;++phase)
    for(fail=0;fail<2;++fail)for(frames=0;frames<=8;++frames) {
        const char *expected;unsigned exportDue=due||phase;int status;
        cursor=0;trace[0]=0;Clock=100;PumpStatus=fail?-1:0;PumpFrames=frames;
        status=run(due,phase);
#if RPI5CYW_TX_CREDIT_SCHEDULING
        expected=fail?"BPEF":(exportDue?"BPETDI":"BPETI");
#else
        expected=fail?(exportDue?"TDBPEF":"TBPEF"):(exportDue?"TDBPEI":"TBPEI");
#endif
        if(status!=(int)fail || strcmp(trace,expected)) {
            printf("FAIL dispatch order: got %s expected %s\n",trace,expected);Failures++;
        }
    }
    if(Failures)return 1;
    puts("PASS: actual post-RX source order, exactly one pump, exporter deadline/phase, zero progress and failure exits.");
    return 0;
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang', choices=['clang', 'cl'])
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    network = (ROOT / 'src/cyw43455/network.c').read_text(encoding='utf-8')
    start = network.index('/* TX-CREDIT-SCHED-BEGIN */', network.index('CywTxDiagRx('))
    end = network.index('        interruptWake=', start)
    source = args.out / 'production-post-rx.c'
    source.write_text(PREFIX + network[start:end] + SUFFIX, encoding='utf-8')
    for mode in (0, 1):
        exe = (args.out / ('tx-dispatch-order-' + str(mode) + '.exe')).resolve()
        define = 'RPI5CYW_TX_CREDIT_SCHEDULING=' + str(mode)
        if args.cc == 'cl':
            flags = ['/nologo','/W4','/WX','/O1','/Zi','/TC','/std:c17',
                     '/fsanitize=address','/D'+define,str(source),'/Fe:'+str(exe)]
        else:
            flags = ['-std=c17','-Wall','-Wextra','-Werror','-O1','-g',
                     '-fsanitize=address,undefined','-D'+define,str(source),'-o',str(exe)]
        subprocess.run([args.cc,*flags],check=True,cwd=ROOT)
        subprocess.run([str(exe)],check=True,cwd=ROOT)

if __name__ == '__main__':
    main()
