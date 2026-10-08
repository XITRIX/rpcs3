void fence::wait_flush()
	{
		while (!flushed)
		{
			utils::pause();
		}
	}