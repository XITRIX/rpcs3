void fence::signal_flushed()
	{
		flushed.release(true);
	}