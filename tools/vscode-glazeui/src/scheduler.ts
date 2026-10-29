// RenderScheduler — en render åt gången per nyckel + debounce (planens
// steg 2: filbevakning + spara får inte ge render-stormar av parallella
// spawns). Sista begäran vinner: kommer nya begäranden medan en render kör
// körs EN gång till när den är klar (koalescerat, aldrig en kö).
// vscode-fri så den kan testas med ren node.

export class RenderScheduler {
    private jobs = new Map<
        string,
        {
            timer?: ReturnType<typeof setTimeout>;
            running: boolean;
            rerun: boolean;
        }
    >();

    schedule(key: string, run: () => Promise<void>, delayMs = 150): void {
        let job = this.jobs.get(key);
        if (!job) {
            job = { running: false, rerun: false };
            this.jobs.set(key, job);
        }
        if (job.timer) clearTimeout(job.timer);
        job.timer = setTimeout(() => {
            job.timer = undefined;
            void this.kick(job, run);
        }, delayMs);
    }

    private async kick(
        job: { running: boolean; rerun: boolean },
        run: () => Promise<void>,
    ): Promise<void> {
        if (job.running) {
            job.rerun = true;
            return;
        }
        job.running = true;
        try {
            do {
                job.rerun = false;
                await run();
            } while (job.rerun);
        } finally {
            job.running = false;
        }
    }

    dispose(key: string): void {
        const job = this.jobs.get(key);
        if (job?.timer) clearTimeout(job.timer);
        this.jobs.delete(key);
    }
}
